#include <vector>
#include <sstream>
#include <iostream>
#include <optional>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include "linenoise.h"
#include "debugger.hpp"
#include "util.cpp"

using namespace unbug;

void debugger::handle_command(const std::string& line) {
    auto args = split(line, ' ');
    auto command = args[0];

    if (is_prefix(command, "continue")) {
        continue_execution();
    } else if (is_prefix(command, "break") && args.size() == 2) {
        std::optional<std::intptr_t> maybe_addr {parse_addr(args[1])};
        if (maybe_addr) {
            set_breakpoint_at_address(maybe_addr.value());
        } else {
            std::cerr << "Bad hex value given\n";
        }
    } else {
        std::cerr << "Unknown command\n";
    }
}

void debugger::run() {
    int wait_status;
    auto options = 0;
    waitpid(m_pid, &wait_status, options);

    char* line = nullptr;
    while((line = linenoise("unbug> ")) != nullptr) {
        handle_command(line);
        linenoiseHistoryAdd(line);
        linenoiseFree(line);
    }
}

void debugger::continue_execution() {
    ptrace(PTRACE_CONT, m_pid, nullptr, nullptr);

    int wait_status;
    auto options = 0;
    waitpid(m_pid, &wait_status, options);
}

void debugger::set_breakpoint_at_address(std::intptr_t addr) {
    std::cout << "Set breakpoint at address 0x" << std::hex << addr << '\n';
    breakpoint bp {m_pid, addr};
    bp.enable();
    m_breakpoints[addr] = bp;
}

void breakpoint::enable() {
    uint64_t int3 = 0xcc;
    auto data = ptrace(PTRACE_PEEKDATA, m_pid, m_addr, nullptr);
    m_saved_data = bottom_byte(data);
    uint64_t data_with_int3 = set_bottom_byte(data, int3);
    ptrace(PTRACE_POKEDATA, m_pid, m_addr, data_with_int3);
    m_enabled = true;
}

void breakpoint::disable() {
    auto data = ptrace(PTRACE_PEEKDATA, m_pid, m_addr, nullptr);
    auto restored_data = set_bottom_byte(data, m_saved_data);
    ptrace(PTRACE_POKEDATA, m_pid, m_addr, restored_data);
    m_enabled = false;
}