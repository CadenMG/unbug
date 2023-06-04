#ifndef UNBUG_DEBUGGER_HPP
#define UNBUG_DEBUGGER_HPP

#include <utility>
#include <unordered_map>
#include <string>
#include <linux/types.h>

#include <breakpoint.hpp>

namespace unbug {
    class debugger {
        std::string m_prog_name;
        pid_t m_pid;

        public:
        debugger (std::string prog_name, pid_t pid)
            : m_prog_name{std::move(prog_name)}, m_pid(pid) {}
        void run();
        void wait_for_signal();
        void set_breakpoint_at_address(std::intptr_t addr);
        void dump_registers();
        uint64_t read_memory(uint64_t addr);
        void write_memory(uint64_t addr, uint64_t val);
        uint64_t get_pc();
        void set_pc(uint64_t pc);
        void step_over_breakpoint();
        
        private:
        void handle_command(const std::string& line);
        void continue_execution();
        std::unordered_map<std::intptr_t, breakpoint> m_breakpoints;
    };
} 

#endif