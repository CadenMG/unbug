#ifndef UNBUG_DEBUGGER_HPP
#define UNBUG_DEBUGGER_HPP

#include <utility>
#include <unordered_map>
#include <string>
#include <linux/types.h>
#include <signal.h>
#include <fcntl.h>

#include "breakpoint.hpp"
#include "dwarf/dwarf++.hh"
#include "elf/elf++.hh"

namespace unbug {
    class debugger {
        public:
            debugger (std::string prog_name, pid_t pid)
                : m_prog_name{std::move(prog_name)}, m_pid(pid) {
                    auto fd = open(m_prog_name.c_str(), O_RDONLY);

                    m_elf = elf::elf{elf::create_mmap_loader(fd)};
                    m_dwarf = dwarf::dwarf{dwarf::elf::create_loader(m_elf)};

                }
            void run();
            void wait_for_signal();
            void set_breakpoint_at_address(std::intptr_t addr);
            void dump_registers();
            void print_source(const std::string& file_name, unsigned line, 
                unsigned n_lines_context);
 
        private:
            void handle_command(const std::string& line);
            void continue_execution();
            uint64_t read_memory(uint64_t addr);
            void write_memory(uint64_t addr, uint64_t val);
            uint64_t get_pc();
            void set_pc(uint64_t pc);
            void step_over_breakpoint();
            dwarf::die get_function_from_pc(uint64_t pc);
            dwarf::line_table::iterator get_line_entry_from_pc(uint64_t pc);
            void initialise_load_address();
            uint64_t offset_load_address(uint64_t addr);
            siginfo_t get_signal_info();
            void handle_sigtrap(siginfo_t info);
            void handle_break(std::vector<std::string>& args);
            void handle_register(std::vector<std::string>& args);
            void handle_memory(std::vector<std::string>& args);

            std::string m_prog_name;
            pid_t m_pid;
            std::unordered_map<std::intptr_t, breakpoint> m_breakpoints;
            dwarf::dwarf m_dwarf;
            elf::elf m_elf;
            uint64_t m_load_address;
    };
} 

#endif