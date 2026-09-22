#include "supervisor.h"
#include <fstream>
#include <stdexcept>

namespace sporemp::worker {
namespace {
std::string ascii_generation(const Generation& generation) {
    std::string result;
    for (auto c : generation_hex(generation)) result += static_cast<char>(c);
    return result;
}
}
Supervisor::Supervisor(const Generation& generation, const std::filesystem::path& run, Log log, Clock clock, ProgressRole role)
    : generation_(generation), sid_(current_sid()), run_(run), log_(std::move(log)), clock_(std::move(clock)), role_(role) {
    started_ = last_heartbeat_ = last_app_ = last_ai_ = clock_();
    if (!engine_.server(pipe_name(generation_, false), sid_, false)) throw std::runtime_error("Private engine pipe unavailable");
    new_control();
    job_ = CreateJobObjectW(nullptr, nullptr);
    if (!job_) throw std::runtime_error("Worker job unavailable");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job_); job_ = nullptr; throw std::runtime_error("Worker job containment unavailable");
    }
    latest_.generation = generation_;
    try {
        log_("worker_supervisor_started", ",\"generation\":\"" + ascii_generation(generation_) + "\"");
        publish("starting");
    } catch (...) { CloseHandle(job_); job_ = nullptr; throw; }
}
Supervisor::~Supervisor() { if (job_) CloseHandle(job_); }
void Supervisor::new_control() {
    ++control_serial_;
    control_.reset(); control_ = std::make_unique<Pipe>();
    if (!control_->server(pipe_name(generation_, true), sid_, true)) throw std::runtime_error("Private operator pipe unavailable");
    control_connected_ = response_ready_ = response_sent_ = false;
    control_since_ = 0;
}
void Supervisor::attach(HANDLE process, DWORD pid) {
    if (!AssignProcessToJobObject(job_, process)) throw std::runtime_error("Could not contain suspended worker in job");
    process_ = process; pid_ = pid; publish("starting");
}
void Supervisor::publish(const char* state) {
    const auto nonce = ascii_generation(generation_);
    std::ofstream out(run_ / L"worker-status.tmp", std::ios::trunc);
    out << "{\"schema_version\":1,\"generation\":\"" << nonce
        << "\",\"supervisor_pid\":" << GetCurrentProcessId() << ",\"game_pid\":" << pid_
        << ",\"state\":\"" << state << "\",\"epoch\":" << latest_.epoch
        << ",\"phase\":" << latest_.values[0] << ",\"app_updates\":" << latest_.values[1]
        << ",\"native_ai_entries\":" << latest_.values[2] << ",\"actor_a\":" << latest_.values[3]
        << ",\"actor_b\":" << latest_.values[4] << ",\"mode\":" << latest_.values[5]
        << ",\"heartbeat_age_ms\":" << clock_() - last_heartbeat_
        << ",\"app_progress_age_ms\":" << clock_() - last_app_
        << ",\"native_progress_age_ms\":" << clock_() - last_ai_
        << ",\"progress_role\":\"" << (role_ == ProgressRole::network_replica ? "network_replica" : "native_simulation") << "\""
        << ",\"projection_state\":" << projection_state_ << ",\"projection_baseline\":" << projection_baseline_
        << ",\"projection_status_age_ms\":" << (last_projection_ ? clock_() - last_projection_ : 0)
        << ",\"uptime_ms\":" << clock_() - started_
        << ",\"desktop_requirement\":\"rendered Windows desktop; qualification pending\"}\n";
    out.close();
    if (!out) throw std::runtime_error("Worker status temporary write failed");
    // Readers that omit FILE_SHARE_DELETE can briefly prevent atomic replacement.
    // Keep the old complete status and retry only that narrow class of errors.
    // This runs on the supervisor thread, never the original engine thread, and
    // uses a real clock so an injected status clock cannot make the wait infinite.
    const auto retry_started = GetTickCount64();
    constexpr ULONGLONG retry_limit_ms = 500;
    constexpr unsigned retry_limit = 25;
    unsigned retries = 0;
    while (!MoveFileExW((run_ / L"worker-status.tmp").c_str(), (run_ / L"worker-status.json").c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto error = GetLastError();
        const bool transient = error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION || error == ERROR_ACCESS_DENIED;
        const auto elapsed = GetTickCount64() - retry_started;
        if (!transient || elapsed >= retry_limit_ms || retries >= retry_limit)
            throw std::runtime_error("Worker status publication failed: win32=" + std::to_string(error) +
                " retries=" + std::to_string(retries) + " elapsed_ms=" + std::to_string(elapsed));
        if (!retries) log_("worker_status_publication_retry", ",\"win32_error\":" + std::to_string(error) +
            ",\"retry_limit_ms\":" + std::to_string(retry_limit_ms));
        ++retries;
        Sleep(static_cast<DWORD>((retry_limit_ms - elapsed) < 20 ? retry_limit_ms - elapsed : 20));
    }
    if (retries) log_("worker_status_publication_recovered", ",\"retries\":" + std::to_string(retries) +
        ",\"elapsed_ms\":" + std::to_string(GetTickCount64() - retry_started));
    last_publish_ = clock_();
}
void Supervisor::stop(const char* reason) {
    // A pending clean shutdown already has a stopping timestamp. It must still
    // escalate to a bounded forced stop if its engine acknowledgement is lost.
    if (failed_) return;
    if (!stopping_) stopping_ = clock_();
    failed_ = true;
    log_("worker_failed", ",\"reason\":\"" + std::string(reason) + "\"");
    // Termination is explicitly recorded as failure, never a successful native save.
    if (process_) TerminateJobObject(job_, 35);
    publish(reason);
}
void Supervisor::tick() {
    const auto now = clock_();
    if (engine_.connected(pid_)) {
        Message message;
        for (unsigned i = 0; i < 8 && engine_.receive(message); ++i) {
            if ((message.kind != Kind::status && message.kind != Kind::reply) || !accept_sequence(message, generation_, incoming_)) {
                stop("invalid_engine_protocol"); return;
            }
            if (message.values[0] == static_cast<uint64_t>(Phase::failed)) { stop("engine_reported_failure"); return; }
            if (message.values[1] < latest_.values[1] || message.values[2] < latest_.values[2]) { stop("regressed_native_progress"); return; }
            if (message.values[1] > latest_.values[1]) last_app_ = now;
            if (message.values[2] > latest_.values[2]) last_ai_ = now;
            if (role_ == ProgressRole::network_replica && message.kind == Kind::status) {
                // Only an authenticated, sequenced engine status describes
                // current baseline application. Reply words 6/7 are request
                // and result; they must never establish or refresh readiness.
                projection_state_ = message.values[6]; projection_baseline_ = message.values[7];
                last_projection_ = now;
                projection_valid_ =
                    ((projection_state_ == static_cast<uint64_t>(ProjectionState::none) ||
                      projection_state_ == static_cast<uint64_t>(ProjectionState::waiting)) && !projection_baseline_) ||
                    (projection_state_ == static_cast<uint64_t>(ProjectionState::connected) && projection_baseline_ != 0);
            }
            latest_ = message; last_heartbeat_ = now;
            if (message.kind == Kind::reply) {
                if (!pending_ || message.values[6] != pending_ || message.op != pending_op_) { stop("unmatched_engine_reply"); return; }
                log_("worker_command_result", ",\"request\":" + std::to_string(pending_) + ",\"op\":\"" + op_name(message.op) + "\",\"result\":\"" + result_name(static_cast<Result>(message.values[7])) + "\"");
                if (pending_control_ == control_serial_ && control_connected_) {
                    reply_ = message; reply_.sequence = 1; response_ready_ = true;
                }
                pending_ = 0;
            }
        }
    }
    // The process handle, checked by NativeHost, is authoritative for crashes.
    if (engine_.failed() && !requested_stop_ && now - last_heartbeat_ > 1000) { stop("engine_ipc_lost"); return; }
    if (!incoming_ && now - started_ > 120000) { stop("initialization_timeout"); return; }
    if (incoming_ && now - last_heartbeat_ > 30000 && !requested_stop_) { stop("engine_update_timeout"); return; }
    if (!control_connected_ && control_->connected()) { control_connected_ = true; control_since_ = now; }
    if (control_connected_ && !response_sent_ && !response_ready_ && !pending_) {
        Message request;
        if (control_->receive(request)) {
            if (request.kind != Kind::command || request.generation != generation_ || request.sequence != 1) { new_control(); return; }
            reply_ = latest_; reply_.kind = Kind::reply; reply_.op = request.op; reply_.sequence = 1;
            reply_.generation = generation_;
            // A locally answered status/refusal has no engine command ID.
            // Latest status word 6 is projection metadata, never a request ID.
            reply_.values[6] = 0; reply_.values[7] = static_cast<uint64_t>(Result::accepted);
            if (request.op == Op::status) response_ready_ = true;
            else if (request.op == Op::shutdown && !requested_stop_ && (!incoming_ || now - last_heartbeat_ > 3000 || engine_.failed())) {
                requested_stop_ = failed_ = true; stopping_ = now; response_ready_ = true;
                log_("worker_forced_stop", ",\"reason\":\"operator_stopped_uninitialized_or_unresponsive_worker\"");
                // A stuck startup dialog cannot receive an engine-thread command.
                // Stop only the owned job and retain a distinct forced-stop result.
                TerminateJobObject(job_, 35);
            }
            else if (!incoming_ || now - last_heartbeat_ > 3000 || requested_stop_ || !engine_.writable()) {
                reply_.values[7] = static_cast<uint64_t>(Result::unavailable); response_ready_ = true;
            } else {
                request.sequence = ++outgoing_; pending_ = request.sequence; pending_op_ = request.op; pending_control_ = control_serial_; request_since_ = now;
                if (!engine_.send(request)) { pending_ = 0; reply_.values[7] = static_cast<uint64_t>(Result::failed); response_ready_ = true; }
                else {
                    log_("worker_command_sent", ",\"request\":" + std::to_string(request.sequence) + ",\"op\":\"" + op_name(request.op) + "\",\"epoch\":" + std::to_string(request.epoch));
                    if (request.op == Op::shutdown) { requested_stop_ = true; stopping_ = now; }
                }
            }
        }
    }
    if (control_connected_ && response_ready_ && !response_sent_ && control_->writable()) response_sent_ = control_->send(reply_);
    if (response_sent_ && control_->writable()) { Message ignored; control_->receive(ignored); }
    if (control_->failed() || (control_connected_ && now - control_since_ > 10000 && !pending_)) new_control();
    if (pending_ && now - request_since_ > 15000) { stop("command_ack_timeout"); return; }
    if (requested_stop_ && now - stopping_ > 15000 && !failed_) {
        log_("worker_forced_stop", ",\"reason\":\"native_shutdown_timeout\"");
        failed_ = true; TerminateJobObject(job_, 35);
    }
    if (now - last_publish_ >= 1000) {
        const char* state = requested_stop_ ? "stopping" : !incoming_ ? "starting" :
            now - last_heartbeat_ > 3000 || now - last_app_ > 3000 ? "unresponsive" :
            latest_.values[0] == static_cast<uint64_t>(Phase::scene) ?
                (role_ == ProgressRole::network_replica ?
                    (projection_valid_ && projection_state_ == static_cast<uint64_t>(ProjectionState::connected) &&
                     now - last_projection_ < 3000 ? "ready" : "replica_waiting") :
                    (now - last_ai_ < 3000 ? "ready" : "simulation_stalled")) :
            latest_.values[0] == static_cast<uint64_t>(Phase::loading) ? "loading" :
            latest_.values[0] == static_cast<uint64_t>(Phase::failed) ? "bridge_failed" : "menu";
        publish(state);
    }
}
void Supervisor::exited(DWORD exit_code) {
    // Drain a final engine acknowledgment already in the local pipe before
    // closing its operator connection. Waiting is bounded and off the engine thread.
    if (requested_stop_) {
        const auto end = GetTickCount64() + 250;
        while (GetTickCount64() < end && (pending_ || response_ready_ || response_sent_)) {
            tick(); Sleep(5);
            if (!pending_ && !control_connected_) break;
        }
    }
    log_("worker_exited", ",\"exit_code\":" + std::to_string(exit_code) + ",\"requested\":" + (requested_stop_ ? "true" : "false"));
    publish(requested_stop_ && failed_ ? "stopped_forced" : exit_code == 0 && requested_stop_ ? "stopped" : "crashed");
}
}
