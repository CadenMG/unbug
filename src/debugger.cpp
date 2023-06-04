#include <vector>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <optional>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include "linenoise.h"
#include "debugger.hpp"
#include "registers.hpp"
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
    } else if (is_prefix(command, "register")) {
        if (args.size() == 2 && is_prefix(args[1], "dump")) {
            dump_registers();
        } else if (args.size() == 3 && is_prefix(args[1], "read")) {
            std::cout << get_register_value(m_pid, get_register_from_name(args[2])) << '\n';
        } else if (args.size() == 4 && is_prefix(args[1], "write")) {
            auto maybe_val {parse_hex(args[3])};
            if (maybe_val) {
                set_register_value(m_pid, get_register_from_name(args[2]), maybe_val.value());
            } else {
                std::cerr << "Bad hex value given\n";
            }
        } else {
            std::cerr << "Unknown register command\n";
        }
    } else if (args.size() > 2 && is_prefix(command, "memory")) {
        auto maybe_addr {parse_addr(args[2])};
        if (maybe_addr) {
            if (is_prefix(args[1], "read")) {
                std::cout << std::hex << read_memory(maybe_addr.value()) << '\n';
            } else if (args.size() == 4 && is_prefix(args[1], "write")) {
                auto maybe_val {parse_hex(args[3])};
                if (maybe_val) {
                    write_memory(maybe_addr.value(), maybe_val.value());
                } else {
                    std::cerr << "Bad hex value given\n";
                }
            } else {
                std::cerr << "Unknown command\n";
            }
        } else {
            std::cerr << "Bad hex value given\n";
        }
    } else {
        std::cerr << "Unknown command\n";
    }
}

void debugger::wait_for_signal() {
    int wait_status;
    auto options = 0;
    waitpid(m_pid, &wait_status, options);
}

void debugger::run() {
    wait_for_signal();
    char* line = nullptr;
    while((line = linenoise("unbug> ")) != nullptr) {
        handle_command(line);
        linenoiseHistoryAdd(line);
        linenoiseFree(line);
    }
}

void debugger::continue_execution() {
    step_over_breakpoint();
    ptrace(PTRACE_CONT, m_pid, nullptr, nullptr);
    wait_for_signal();
}

void debugger::dump_registers() {
    for (const auto& rd : g_register_descriptors) {
        std::cout << rd.name << ": 0x" << std::setfill('0')
            << std::setw(16) << std::hex << get_register_value(m_pid, rd.r) << '\n';
    }
}

void debugger::set_breakpoint_at_address(std::intptr_t addr) {
    std::cout << "Set breakpoint at address 0x" << std::hex << addr << '\n';
    breakpoint bp {m_pid, addr};
    bp.enable();
    m_breakpoints[addr] = bp;
}

uint64_t debugger::read_memory(uint64_t addr) {
    return ptrace(PTRACE_PEEKDATA, m_pid, addr, nullptr);
}

void debugger::write_memory(uint64_t addr, uint64_t val) {
    ptrace(PTRACE_POKEDATA, m_pid, addr, val);
}

uint64_t debugger::get_pc() {
    return get_register_value(m_pid, reg::rip);
}

void debugger::set_pc(uint64_t pc) {
    set_register_value(m_pid, reg::rip, pc);
}

void debugger::step_over_breakpoint() {
    auto possible_breakpoint_location = get_pc() - 1;

    if (m_breakpoints.count(possible_breakpoint_location)) {
        auto& bp = m_breakpoints[possible_breakpoint_location];

        if (bp.is_enabled()) {
            auto prev_instr_addr = possible_breakpoint_location;
            set_pc(prev_instr_addr);

            bp.disable();
            ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, nullptr);
            wait_for_signal();
            bp.enable();
        }
    }
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