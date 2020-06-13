#ifndef CXXRTLSimulation_hpp
#define CXXRTLSimulation_hpp

#include <fstream>
#include <ostream>

#include "Simulation.hpp"

#include "cxxrtl_sim.h"

#if VM_TRACE
#include "cxxrtl_vcd.h"
#endif

class CXXRTLSimulation: public Simulation {

public:
    void forward_cmd_args(int argc, const char * argv[]) override {};

    void preload_cpu_program(const std::vector<uint8_t> &program) override;
    void step(uint64_t time) override;

#if VM_TRACE
    void trace(const std::string &filename) override;
#endif

    uint8_t r() const override;
    uint8_t g() const override;
    uint8_t b() const override;

    bool hsync() const override;
    bool vsync() const override;

    void final() override;

    bool finished() const override;

private:
    cxxrtl_design::p_ics32__tb top;

#if VM_TRACE
    cxxrtl::vcd_writer vcd;
    std::ofstream vcd_stream;

    void update_trace(uint64_t time);
#endif
};

#endif /* CXXRTLSimulation_hpp */
