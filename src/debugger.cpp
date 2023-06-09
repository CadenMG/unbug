#include <vector>
#include <sstream>
#include <fstream>
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

void debugger::handle_break(std::vector<std::string>& args) {
    std::optional<std::intptr_t> maybe_addr {parse_addr(args[0])};
    if (maybe_addr) {
        set_breakpoint_at_address(maybe_addr.value());
    } else {
        std::cerr << "Bad hex value given\n";
    }
}

void debugger::handle_register(std::vector<std::string>& args) {
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
}

void debugger::handle_memory(std::vector<std::string>& args) {
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
}

void debugger::handle_stepi() {
    single_step_instruction_with_breakpoint_check();
    auto line_entry = get_line_entry_from_pc(get_pc());
    print_source(line_entry->file->path, line_entry->line);
}

void debugger::handle_command(const std::string& line) {
    auto args = split(line, ' ');
    if (args.size() == 0) {
        std::cerr << "Empty command\n";
        return;
    }
    auto command = args[0];
    if (is_prefix(command, "continue")) {
        continue_execution();
    } else if (is_prefix(command, "break") && args.size() == 2) {
        handle_break(args);
    } else if (is_prefix(command, "register")) {
        handle_register(args);
    } else if (args.size() > 2 && is_prefix(command, "memory")) {
        handle_memory(args);
    } else if (is_prefix(command, "step")) {
        step_in();
    } else if (is_prefix(command, "next")) {
        step_over();
    } else if (is_prefix(command, "finish")) {
        step_out();
    } else if (is_prefix(command, "stepi")) {
        handle_stepi();
    } else {
        std::cerr << "Unknown command\n";
    }
}

void debugger::wait_for_signal() {
    int wait_status;
    auto options = 0;
    waitpid(m_pid, &wait_status, options);

    auto siginfo = get_signal_info();

    switch (siginfo.si_signo) {
    case SIGTRAP:
        handle_sigtrap(siginfo);
        break;
    case SIGSEGV:
        std::cout << "SEGFAULT. Reason: " << siginfo.si_code << '\n';
        break;
    default:
        std::cout << "Got signal: " << strsignal(siginfo.si_code) << '\n';
        break;
    }
}

void debugger::run() {
    wait_for_signal();
    initialise_load_address();
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
    if (m_breakpoints.count(get_pc())) {
        auto& bp = m_breakpoints[get_pc()];
        if (bp.is_enabled()) {
            bp.disable();
            ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, nullptr);
            wait_for_signal();
            bp.enable();
        }
    }
}

dwarf::die debugger::get_function_from_pc(uint64_t pc) {
    for (auto &cu : m_dwarf.compilation_units()) {
        if (die_pc_range(cu.root()).contains(pc)) {
            for (const auto& die : cu.root()) {
                if (die.tag == dwarf::DW_TAG::subprogram) {
                    if (die_pc_range(die).contains(pc)) {
                        return die;
                    }
                }
            }
        }
    }
    throw std::out_of_range{"Cannot find function"};
}

dwarf::line_table::iterator debugger::get_line_entry_from_pc(uint64_t pc) {
    for (auto &cu : m_dwarf.compilation_units()) {
        if (die_pc_range(cu.root()).contains(pc)) {
            auto &lt = cu.get_line_table();
            auto it = lt.find_address(pc);
            if (it == lt.end()) {
                throw std::out_of_range{"Cannot find line entry"};
            } else {
                return it;
            }
        }
    }
    throw std::out_of_range{"Cannot find line entry"};
}

void debugger::initialise_load_address() {
    if (m_elf.get_hdr().type == elf::et::dyn) {
        std::ifstream map("/proc/" + std::to_string(m_pid) + "/maps");
 
        std::string addr;
        std::getline(map, addr, '-');

        m_load_address = std::stol(addr, 0, 16);
    }
}

uint64_t debugger::offset_load_address(uint64_t addr) {
    return addr - m_load_address;
}

void debugger::print_source(const std::string& file_name, unsigned line, 
    unsigned n_lines_context) {
        std::ifstream file {file_name};

        auto start_line = line <= n_lines_context ? 1 : line - n_lines_context;
        auto end_line = line + n_lines_context + 
            (line < n_lines_context ? n_lines_context - line : 0) + 1;

        char c{};
        auto current_line = 1u;
        while (current_line != start_line && file.get(c)) {
            if (c == '\n') {
                ++current_line;
            }
        }

        std::cout << (current_line == line ? "> " : " ");

        while (current_line <= end_line && file.get(c)) {
            std::cout << c;
            if (c == '\n') {
                ++current_line;
                std::cout << (current_line == line ? "> " : " ");
            }
        }

        std::cout << '\n';
}

siginfo_t debugger::get_signal_info() {
    siginfo_t info;
    ptrace(PTRACE_GETSIGINFO, m_pid, nullptr, &info);
    return info;
}

void debugger::handle_sigtrap(siginfo_t info) {
    switch (info.si_code) {
    case SI_KERNEL:
    case TRAP_BRKPT:
    {
        set_pc(get_pc() - 1);
        std::cout << "Hit breakpoint at 0x" << std::hex << get_pc() << '\n';
        auto offset_pc = offset_load_address(get_pc());
        auto line_entry = get_line_entry_from_pc(offset_pc);
        print_source(line_entry->file->path, line_entry->line);
        return;
    }
    case TRAP_TRACE:
        return;
    default:
        std::cout << "Unkown SIGTRAP code: " << info.si_code << '\n';
        return;
    }
}

void debugger::single_step_instruction() {
    ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, nullptr);
}

void debugger::single_step_instruction_with_breakpoint_check() {
    if (m_breakpoints.count(get_pc())) {
        step_over_breakpoint();
    } else {
        single_step_instruction();
    }
}

void debugger::step_out() {
    auto frame_pointer = get_register_value(m_pid, reg::rbp);
    auto return_address = read_memory(frame_pointer + 8);

    auto should_remove_breakpoint = false;
    if (!m_breakpoints.count(return_address)) {
        set_breakpoint_at_address(return_address);
        should_remove_breakpoint = true;
    }

    continue_execution();

    if (should_remove_breakpoint) {
        remove_breakpoint(return_address);
    }
}

void debugger::step_in() {
    auto line = get_line_entry_from_pc(get_offset_pc())->line;

    while (get_line_entry_from_pc(get_offset_pc())->line == line) {
        single_step_instruction_with_breakpoint_check();
    }

    auto line_entry = get_line_entry_from_pc(get_offset_pc());
    print_source(line_entry->file->path, line_entry->line);
}

void debugger::step_over() {
    auto func = get_function_from_pc(get_offset_pc());
    auto func_entry = at_low_pc(func);
    auto func_end = at_high_pc(func);

    auto line = get_line_entry_from_pc(func_entry);
    auto start_line = get_line_entry_from_pc(get_offset_pc());

    std::vector<std::intptr_t> to_delete{};

    while (line->address < func_end) {
        auto load_address = offset_dwarf_address(line->address);
        if (line->address != start_line->address && !m_breakpoints.count(load_address)) {
            set_breakpoint_at_address(load_address);
            to_delete.push_back(load_address);
        }
        ++line;
    }

    auto frame_pointer = get_register_value(m_pid, reg::rbp);
    auto return_address = read_memory(frame_pointer + 8);
    if (!m_breakpoints.count(return_address)) {
        set_breakpoint_at_address(return_address);
        to_delete.push_back(return_address);
    }

    continue_execution();

    for (auto addr : to_delete) {
        remove_breakpoint(addr);
    }
}

uint64_t debugger::get_offset_pc() {
    return offset_load_address(get_pc());
}

uint64_t debugger::offset_dwarf_address(uint64_t addr) {
    return addr + m_load_address;
}

void debugger::remove_breakpoint(std::intptr_t addr) {
    if (m_breakpoints.at(addr).is_enabled()) {
        m_breakpoints.at(addr).disable();
    }
    m_breakpoints.erase(addr);
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