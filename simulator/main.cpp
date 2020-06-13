#include <SDL.h>

#include <fstream>
#include <iterator>
#include <vector>
#include <iostream>
#include <memory>

#ifdef SIM_VERILATOR

#include "VerilatorSimulation.hpp"
typedef VerilatorSimulation SimulationImpl;

#elif defined(SIM_CXXRTL)

#include "CXXRTLSimulation.hpp"
typedef CXXRTLSimulation SimulationImpl;

#else

#error Expected one of SIM_VERILATOR or SIM_CXXRTL to be defined

#endif

int main(int argc, const char * argv[]) {
    SimulationImpl sim;
    sim.forward_cmd_args(argc, argv);

    // expecting the test program as first argument for now

    if (argc < 2) {
        std::cout << "Usage: ics32-sim <test-program>" << std::endl;
        return EXIT_SUCCESS;
    }

    // 1. load test program...

    auto cpu_program_path = argv[1];

    std::ifstream cpu_program_stream(cpu_program_path, std::ios::binary);
    if (cpu_program_stream.fail()) {
        std::cerr << "Failed to open file: " << cpu_program_path << std::endl;
        return EXIT_FAILURE;
    }

    std::vector<uint8_t> cpu_program(std::istreambuf_iterator<char>(cpu_program_stream), {});

    if (cpu_program.size() % 4) {
        std::cerr << "Program has irregular size: " << cpu_program.size() << std::endl;
        return EXIT_FAILURE;
    }

    sim.preload_cpu_program(cpu_program);

    // 2. present an SDL window to simulate video output

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init() failed: " << SDL_GetError() << std::endl;
        return EXIT_FAILURE;
    }

    // SDL defaults to Metal API on MacOS and is incredibly slow to run SDL_RenderPresent()
    // Hint to use OpenGL if possible
#if TARGET_OS_MAC
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
#endif

    // 848x480 is assumed (smaller video modes will still appear correctly)

    const auto active_width = 848;
    const auto offscreen_width = 240;
    const auto total_width = active_width + offscreen_width;

    const auto active_height = 480;
    const auto offscreen_height = 37;
    const auto total_height = active_height + offscreen_height;

    auto window = SDL_CreateWindow(
       "ics32-sim (verilator)",
       SDL_WINDOWPOS_CENTERED,
       SDL_WINDOWPOS_CENTERED,
       total_width,
       total_height,
       SDL_WINDOW_SHOWN
   );

    auto renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_RenderSetScale(renderer, 1, 1);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_Texture *texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STATIC,
        total_width,
        total_height
    );

    const auto stride = total_width * 3;

    uint8_t *pixels = (uint8_t *)std::calloc(stride * total_height * 3, sizeof(uint8_t));
    assert(pixels);

    int current_x = 0;
    int current_y = 0;

#if VM_TRACE
    sim.trace("ics.vcd");
#endif

    bool vga_hsync_previous = false;
    bool vga_vsync_previous = false;

    sim.clk_1x = 0;
    sim.clk_2x = 0;

    uint64_t time = 0;

    const auto sdl_poll_interval = 10000;
    auto sdl_poll_counter = sdl_poll_interval;

    while (!sim.finished()) {
        // clock negedge
        sim.clk_2x = 0;
        sim.step(time);
        time++;

        // clock posedge
        sim.clk_2x = 1;
        sim.clk_1x = time & 2;
        sim.step(time);
        time++;

        auto extend_color = [] (uint8_t component) {
            return component | component << 4;
        };

        // render current VGA output pixel
        size_t pixel_base = (current_y * total_width + current_x) * 3;
        pixels[pixel_base++] = extend_color(sim.r());
        pixels[pixel_base++] = extend_color(sim.g());
        pixels[pixel_base++] = extend_color(sim.b());

        current_x++;

        if (sim.hsync() && !vga_hsync_previous) {
            current_x = 0;
            current_y++;
        }

        vga_hsync_previous = sim.hsync();

        if (sim.vsync() && !vga_vsync_previous) {
            current_y = 0;

            SDL_UpdateTexture(texture, NULL, pixels, stride);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);

            // input test (using mocked 3-button setup as the iCEBreaker)
            SDL_PumpEvents();
            const Uint8 *state = SDL_GetKeyboardState(NULL);

            sim.button_1 = state[SDL_SCANCODE_LEFT];
            sim.button_2 = state[SDL_SCANCODE_RSHIFT];
            sim.button_3 = state[SDL_SCANCODE_RIGHT];

            std::cout << "frame, step: " << time << "\n";
        }

        vga_vsync_previous = sim.vsync();

        // exit checking

        if (!(--sdl_poll_counter)) {
            SDL_Event e;
            SDL_PollEvent(&e);

            if (e.type == SDL_QUIT) {
                break;
            }

            sdl_poll_counter = sdl_poll_interval;
        }
    };

    sim.final();

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::free(pixels);

    return EXIT_SUCCESS;
}
