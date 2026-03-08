#pragma once

#include <cstdint>
#include <array>
#include <functional>
#include "Serializer.h"

class APU {
public:
    APU() : cycle_counter(0), frame_counter_mode(0),
        frame_counter_irq_inhibit(false), frame_counter_irq_flag(false),
        frame_counter_step(0), frame_counter_reset_delay(0) {
        reset();
    }

    void reload_length_counter(int channel, uint8_t value) {
        switch (channel) {
        case 0:
            pulse1.length_counter = value;
            break;
        case 1:
            pulse2.length_counter = value;
            break;
        case 2:
            triangle.length_counter = value;
            break;
        case 3:
            noise.length_counter = value;
            break;
        }
    }

    void step_frame_sequencer() {
        clock_quarter_frame();
        clock_half_frame();
    }

    void reset() {
        // Reset all channels
        pulse1 = PulseChannel();
        pulse2 = PulseChannel();
        triangle = TriangleChannel();
        noise = NoiseChannel();
        dmc = DMCChannel();

        // Reset frame counter
        cycle_counter = 0;
        frame_counter_mode = 0;
        frame_counter_irq_flag = false;
        frame_counter_step = 0;
        frame_counter_reset_delay = 0;

        // Write $00 to all registers
        for (uint16_t addr = 0x4000; addr <= 0x4013; addr++) {
            write_register(addr, 0);
        }
        write_register(0x4015, 0);
        write_register(0x4017, 0);
        frame_counter_irq_inhibit = true;
    }

    void step() {
        // Handle frame counter reset delay
        if (frame_counter_reset_delay > 0) {
            frame_counter_reset_delay--;
            if (frame_counter_reset_delay == 0) {
                cycle_counter = 0;
                if (frame_counter_mode == 1) {
                    // 5-step mode clocks immediately
                    clock_quarter_frame();
                    clock_half_frame();
                }
            }
        }

        // Clock triangle every cycle (runs at CPU rate)
        triangle.clock_timer();

        // Clock pulse and noise every other cycle (runs at CPU/2 rate)
        if (cycle_counter % 2 == 0) {
            pulse1.clock_timer();
            pulse2.clock_timer();
            noise.clock_timer();
        }

        // Clock DMC every cycle
        dmc.clock_timer();

        // Frame counter sequences
        if (frame_counter_mode == 0) {
            // 4-step mode
            switch (cycle_counter) {
            case 7457:
                clock_quarter_frame();
                break;
            case 14913:
                clock_quarter_frame();
                clock_half_frame();
                break;
            case 22371:
                clock_quarter_frame();
                break;
            case 29828:
                clock_quarter_frame();
                clock_half_frame();
                if (!frame_counter_irq_inhibit) {
                    frame_counter_irq_flag = true;
                }
                break;
            case 29829:
                if (!frame_counter_irq_inhibit) {
                }
                break;
            case 29830:
                if (!frame_counter_irq_inhibit) {
                }
                cycle_counter = 0;
                return;  // Don't increment counter
            }
        }
        else {
            // 5-step mode
            switch (cycle_counter) {
            case 7457:
                clock_quarter_frame();
                break;
            case 14913:
                clock_quarter_frame();
                clock_half_frame();
                break;
            case 22371:
                clock_quarter_frame();
                break;
            case 29829:
                clock_quarter_frame();
                break;
            case 37281:
                clock_quarter_frame();
                clock_half_frame();
                break;
            case 37282:
                cycle_counter = 0;
                return;  // Don't increment counter
            }
        }

        cycle_counter++;
    }

    void write_register(uint16_t address, uint8_t value) {
        if (address >= 0x4000 && address <= 0x4003) {
            // Pulse 1
            pulse1.write_register(address - 0x4000, value);
        }
        else if (address >= 0x4004 && address <= 0x4007) {
            // Pulse 2
            pulse2.write_register(address - 0x4004, value);
        }
        else if (address >= 0x4008 && address <= 0x400B) {
            // Triangle
            triangle.write_register(address - 0x4008, value);
        }
        else if (address >= 0x400C && address <= 0x400F) {
            // Noise
            noise.write_register(address - 0x400C, value);
        }
        else if (address >= 0x4010 && address <= 0x4013) {
            // DMC
            dmc.write_register(address - 0x4010, value);
        }
        else if (address == 0x4015) {
            // Status register (write)
            pulse1.enabled = (value & 0x01) != 0;
            pulse2.enabled = (value & 0x02) != 0;
            triangle.enabled = (value & 0x04) != 0;
            noise.enabled = (value & 0x08) != 0;
            dmc.set_enabled((value & 0x10) != 0);

            // Clear length counters if disabled
            if (!pulse1.enabled) pulse1.length_counter = 0;
            if (!pulse2.enabled) pulse2.length_counter = 0;
            if (!triangle.enabled) triangle.length_counter = 0;
            if (!noise.enabled) noise.length_counter = 0;

            // Clear DMC IRQ flag
            dmc.clear_irq_flag();
        }
        else if (address == 0x4017) {
            // Frame counter
            frame_counter_mode = (value >> 7) & 1;
            frame_counter_irq_inhibit = (value & 0x40) != 0;

            // Clear IRQ flag if inhibit is set
            if (frame_counter_irq_inhibit) {
                frame_counter_irq_flag = false;
            }

            // Reset happens after 3 or 4 CPU cycles
            frame_counter_reset_delay = 3;
        }
    }

    uint8_t read_register(uint16_t address) {
        if (address == 0x4015) {
            // Status register (read)
            uint8_t status = 0;

            if (pulse1.length_counter > 0) status |= 0x01;
            if (pulse2.length_counter > 0) status |= 0x02;
            if (triangle.length_counter > 0) status |= 0x04;
            if (noise.length_counter > 0) status |= 0x08;
            if (dmc.get_bytes_remaining() > 0) status |= 0x10;
            if (frame_counter_irq_flag) status |= 0x40;
            if (dmc.get_irq_flag()) status |= 0x80;

            // Reading clears frame counter IRQ flag
            frame_counter_irq_flag = false;

            return status;
        }

        return 0;
    }

    uint8_t peek_register(uint16_t address) {
        if (address == 0x4015) {
            // Status register (read)
            uint8_t status = 0;

            if (pulse1.length_counter > 0) status |= 0x01;
            if (pulse2.length_counter > 0) status |= 0x02;
            if (triangle.length_counter > 0) status |= 0x04;
            if (noise.length_counter > 0) status |= 0x08;
            if (dmc.get_bytes_remaining() > 0) status |= 0x10;
            if (frame_counter_irq_flag) status |= 0x40;
            if (dmc.get_irq_flag()) status |= 0x80;

            return status;
        }

        return 0;
    }

    float get_output() {
        return mix_output();
    }

    // Set DMC memory read callback
    void set_dmc_read_callback(std::function<uint8_t(uint16_t)> cb) {
        dmc.read_memory = cb;
    }

    // Check if IRQ should be triggered
    bool get_irq_flag() const {
        return frame_counter_irq_flag || dmc.get_irq_flag();
    }

    void Serialize(Serializer& serializer) {
        PulseChannelState pulse1_state = {
            pulse1.envelope_start_flag,
            pulse1.envelope_divider,
            pulse1.envelope_decay_level,
            pulse1.envelope_loop,
            pulse1.envelope_period,
            pulse1.constant_volume,
            pulse1.constant_volume_period,
            pulse1.sweep_enabled,
            pulse1.sweep_divider,
            pulse1.sweep_period,
            pulse1.sweep_negate,
            pulse1.sweep_shift,
            pulse1.sweep_reload,
            pulse1.timer_period,
            pulse1.timer_counter,
            pulse1.duty_cycle,
            pulse1.sequence_position,
            pulse1.length_counter,
            pulse1.enabled
		};
        serializer.Write(pulse1_state);
        PulseChannelState pulse2_state = {
            pulse2.envelope_start_flag,
            pulse2.envelope_divider,
            pulse2.envelope_decay_level,
            pulse2.envelope_loop,
            pulse2.envelope_period,
            pulse2.constant_volume,
            pulse2.constant_volume_period,
            pulse2.sweep_enabled,
            pulse2.sweep_divider,
            pulse2.sweep_period,
            pulse2.sweep_negate,
            pulse2.sweep_shift,
            pulse2.sweep_reload,
            pulse2.timer_period,
            pulse2.timer_counter,
            pulse2.duty_cycle,
            pulse2.sequence_position,
            pulse2.length_counter,
            pulse2.enabled
        };
        serializer.Write(pulse2_state);
        TriangleChannelState triangle_state = {
            triangle.linear_counter,
            triangle.linear_counter_reload,
            triangle.linear_counter_reload_flag,
            triangle.linear_counter_control,
            triangle.timer_period,
            triangle.timer_counter,
            triangle.sequence_position,
			triangle.length_counter,
            triangle.enabled
        };
        serializer.Write(triangle_state);
        NoiseChannelState noise_state = {
            noise.envelope_loop,
            noise.constant_volume,
            noise.volume_envelope,
            noise.length_counter_halt,
            noise.mode_flag,
            noise.timer_period_index,
            noise.timer_period,
            noise.timer_counter,
            noise.shift_register,
            noise.envelope_counter,
            noise.envelope_decay,
            noise.envelope_start_flag,
            noise.length_counter,
            noise.enabled
        };
        serializer.Write(noise_state);
        DMCChannelState dmc_state = {
            dmc.irq_enabled,
            dmc.irq_flag,
            dmc.loop,
            dmc.timer_period,
            dmc.timer_counter,
            dmc.sample_address,
            dmc.sample_length,
            dmc.current_address,
            dmc.bytes_remaining,
            dmc.output_level,
            dmc.sample_buffer,
            dmc.sample_buffer_empty,
            dmc.shift_register,
            dmc.bits_remaining,
            dmc.silence_flag,
            dmc.enabled
        };
        serializer.Write(dmc_state);
        serializer.Write(cycle_counter);
        serializer.Write(frame_counter_mode);
        serializer.Write(frame_counter_irq_inhibit);
        serializer.Write(frame_counter_irq_flag);
        serializer.Write(frame_counter_step);
		serializer.Write(frame_counter_reset_delay);
    }

    void Deserialize(Serializer& serializer) {
        PulseChannelState pulse1_state;
        serializer.Read(pulse1_state);
        pulse1.envelope_start_flag = pulse1_state.envelope_start_flag;
        pulse1.envelope_divider = pulse1_state.envelope_divider;
        pulse1.envelope_decay_level = pulse1_state.envelope_decay_level;
        pulse1.envelope_loop = pulse1_state.envelope_loop;
        pulse1.envelope_period = pulse1_state.envelope_period;
        pulse1.constant_volume = pulse1_state.constant_volume;
        pulse1.constant_volume_period = pulse1_state.constant_volume_period;
        pulse1.sweep_enabled = pulse1_state.sweep_enabled;
        pulse1.sweep_divider = pulse1_state.sweep_divider;
        pulse1.sweep_period = pulse1_state.sweep_period;
        pulse1.sweep_negate = pulse1_state.sweep_negate;
        pulse1.sweep_shift = pulse1_state.sweep_shift;
        pulse1.sweep_reload = pulse1_state.sweep_reload;
        pulse1.timer_period = pulse1_state.timer_period;
        pulse1.timer_counter = pulse1_state.timer_counter;
        pulse1.duty_cycle = pulse1_state.duty_cycle;
        pulse1.sequence_position = pulse1_state.sequence_position;
        pulse1.length_counter = pulse1_state.length_counter;
        pulse1.enabled = pulse1_state.enabled;
        PulseChannelState pulse2_state;
        serializer.Read(pulse2_state);
        pulse2.envelope_start_flag = pulse2_state.envelope_start_flag;
        pulse2.envelope_divider = pulse2_state.envelope_divider;
        pulse2.envelope_decay_level = pulse2_state.envelope_decay_level;
        pulse2.envelope_loop = pulse2_state.envelope_loop;
        pulse2.envelope_period = pulse2_state.envelope_period;
        pulse2.constant_volume = pulse2_state.constant_volume;
        pulse2.constant_volume_period = pulse2_state.constant_volume_period;
        pulse2.sweep_enabled = pulse2_state.sweep_enabled;
        pulse2.sweep_divider = pulse2_state.sweep_divider;
        pulse2.sweep_period = pulse2_state.sweep_period;
        pulse2.sweep_negate = pulse2_state.sweep_negate;
        pulse2.sweep_shift = pulse2_state.sweep_shift;
        pulse2.sweep_reload = pulse2_state.sweep_reload;
        pulse2.timer_period = pulse2_state.timer_period;
		pulse2.timer_counter = pulse2_state.timer_counter;
        pulse2.duty_cycle = pulse2_state.duty_cycle;
        pulse2.sequence_position = pulse2_state.sequence_position;
        pulse2.length_counter = pulse2_state.length_counter;
        pulse2.enabled = pulse2_state.enabled;
        TriangleChannelState triangle_state;
        serializer.Read(triangle_state);
        triangle.linear_counter = triangle_state.linear_counter;
        triangle.linear_counter_reload = triangle_state.linear_counter_reload;
        triangle.linear_counter_reload_flag = triangle_state.linear_counter_reload_flag;
        triangle.linear_counter_control = triangle_state.linear_counter_control;
        triangle.timer_period = triangle_state.timer_period;
        triangle.timer_counter = triangle_state.timer_counter;
        triangle.sequence_position = triangle_state.sequence_position;
        triangle.length_counter = triangle_state.length_counter;
        triangle.enabled = triangle_state.enabled;
        NoiseChannelState noise_state;
        serializer.Read(noise_state);
        noise.envelope_loop = noise_state.envelope_loop;
        noise.constant_volume = noise_state.constant_volume;
        noise.volume_envelope = noise_state.volume_envelope;
        noise.length_counter_halt = noise_state.length_counter_halt;
        noise.mode_flag = noise_state.mode_flag;
        noise.timer_period_index = noise_state.timer_period_index;
        noise.timer_period = noise_state.timer_period;
        noise.timer_counter = noise_state.timer_counter;
        noise.shift_register = noise_state.shift_register;
        noise.envelope_counter = noise_state.envelope_counter;
        noise.envelope_decay = noise_state.envelope_decay;
        noise.envelope_start_flag = noise_state.envelope_start_flag;
        noise.length_counter = noise_state.length_counter;
        noise.enabled = noise_state.enabled;
        DMCChannelState dmc_state;
        serializer.Read(dmc_state);
        dmc.irq_enabled = dmc_state.irq_enabled;
        dmc.irq_flag = dmc_state.irq_flag;
        dmc.loop = dmc_state.loop;
        dmc.timer_period = dmc_state.timer_period;
        dmc.timer_counter = dmc_state.timer_counter;
        dmc.sample_address = dmc_state.sample_address;
        dmc.sample_length = dmc_state.sample_length;
        dmc.current_address = dmc_state.current_address;
        dmc.bytes_remaining = dmc_state.bytes_remaining;
        dmc.output_level = dmc_state.output_level;
        dmc.sample_buffer = dmc_state.sample_buffer;
        dmc.sample_buffer_empty = dmc_state.sample_buffer_empty;
		dmc.shift_register = dmc_state.shift_register;
        dmc.bits_remaining = dmc_state.bits_remaining;
        dmc.silence_flag = dmc_state.silence_flag;
        dmc.enabled = dmc_state.enabled;
        serializer.Read(cycle_counter);
        serializer.Read(frame_counter_mode);
        serializer.Read(frame_counter_irq_inhibit);
        serializer.Read(frame_counter_irq_flag);
        serializer.Read(frame_counter_step);
		serializer.Read(frame_counter_reset_delay);
    }

private:
    // Cycle counter for frame sequencer
    uint32_t cycle_counter;

    // Frame counter state
    uint8_t frame_counter_mode;  // 0 = 4-step, 1 = 5-step
    bool frame_counter_irq_inhibit;
    bool frame_counter_irq_flag;
    uint8_t frame_counter_step;
    uint8_t frame_counter_reset_delay;
    // ===== PULSE CHANNELS (2x) =====
    struct PulseChannel {
        // Envelope
        bool envelope_start_flag;
        uint8_t envelope_divider;
        uint8_t envelope_decay_level;
        bool envelope_loop;  // Also doubles as length counter halt
        uint8_t envelope_period;
        bool constant_volume;
        uint8_t constant_volume_period;

        // Sweep unit
        bool sweep_enabled;
        uint8_t sweep_divider;
        uint8_t sweep_period;
        bool sweep_negate;
        uint8_t sweep_shift;
        bool sweep_reload;

        // Timer
        uint16_t timer_period;
        uint16_t timer_counter;

        // Sequencer
        uint8_t duty_cycle;  // 0-3
        uint8_t sequence_position;  // 0-7

        // Length counter
        uint8_t length_counter;

        // Control
        bool enabled;

        PulseChannel() : envelope_start_flag(false), envelope_divider(0),
            envelope_decay_level(0), envelope_loop(false),
            envelope_period(0), constant_volume(false),
            constant_volume_period(0), sweep_enabled(false),
            sweep_divider(0), sweep_period(0), sweep_negate(false),
            sweep_shift(0), sweep_reload(false), timer_period(0),
            timer_counter(0), duty_cycle(0), sequence_position(0),
            length_counter(0), enabled(false) {
        }

        uint8_t get_output() {
            // Silence conditions
            if (!enabled) return 0;
            if (length_counter == 0) return 0;
            if (timer_period < 8) return 0;  // Ultrasonic
            if (timer_period > 0x7FF) return 0;  // Too low

            // Duty cycle lookup
            // Duty 0: 12.5% (01000000)
            // Duty 1: 25%   (01100000)
            // Duty 2: 50%   (01111000)
            // Duty 3: 75%   (10011111)
            static const uint8_t duty_table[4][8] = {
                {0, 1, 0, 0, 0, 0, 0, 0},  // 12.5%
                {0, 1, 1, 0, 0, 0, 0, 0},  // 25%
                {0, 1, 1, 1, 1, 0, 0, 0},  // 50%
                {1, 0, 0, 1, 1, 1, 1, 1}   // 75% (negated 25%)
            };

            if (duty_table[duty_cycle][sequence_position] == 0) {
                return 0;
            }

            // Return volume (envelope or constant)
            if (constant_volume) {
                return constant_volume_period;
            }
            else {
                return envelope_decay_level;
            }
        }

        void clock_timer() {
            if (timer_counter == 0) {
                timer_counter = timer_period;
                sequence_position = (sequence_position + 1) & 7;
            }
            else {
                timer_counter--;
            }
        }

        void clock_envelope() {
            if (envelope_start_flag) {
                envelope_start_flag = false;
                envelope_decay_level = 15;
                envelope_divider = envelope_period;
            }
            else {
                if (envelope_divider == 0) {
                    envelope_divider = envelope_period;

                    if (envelope_decay_level > 0) {
                        envelope_decay_level--;
                    }
                    else if (envelope_loop) {
                        envelope_decay_level = 15;
                    }
                }
                else {
                    envelope_divider--;
                }
            }
        }

        void clock_sweep(bool is_channel_1) {
            // Calculate target period
            uint16_t change_amount = timer_period >> sweep_shift;
            uint16_t target_period;

            if (sweep_negate) {
                target_period = timer_period - change_amount;
                // Pulse 1 uses one's complement, Pulse 2 uses two's complement
                if (is_channel_1) {
                    target_period--;
                }
            }
            else {
                target_period = timer_period + change_amount;
            }

            // Muting conditions
            bool muting = (timer_period < 8) || (target_period > 0x7FF);

            // Update sweep divider
            if (sweep_divider == 0 && sweep_enabled && !muting) {
                timer_period = target_period;
            }

            if (sweep_divider == 0 || sweep_reload) {
                sweep_divider = sweep_period;
                sweep_reload = false;
            }
            else {
                sweep_divider--;
            }
        }

        void clock_length_counter() {
            if (!envelope_loop && length_counter > 0) {
                length_counter--;
            }
        }

        void write_register(uint8_t reg, uint8_t value) {
            switch (reg) {
            case 0:  // $4000/$4004 - Duty, envelope
                duty_cycle = (value >> 6) & 3;
                envelope_loop = (value & 0x20) != 0;
                constant_volume = (value & 0x10) != 0;
                envelope_period = value & 0x0F;
                constant_volume_period = value & 0x0F;
                break;

            case 1:  // $4001/$4005 - Sweep
                sweep_enabled = (value & 0x80) != 0;
                sweep_period = (value >> 4) & 7;
                sweep_negate = (value & 0x08) != 0;
                sweep_shift = value & 7;
                sweep_reload = true;
                break;

            case 2:  // $4002/$4006 - Timer low
                timer_period = (timer_period & 0xFF00) | value;
                break;

            case 3:  // $4003/$4007 - Length counter load, timer high
                timer_period = (timer_period & 0x00FF) | ((value & 7) << 8);

                // Load length counter
                if (enabled) {
                    static const uint8_t length_table[32] = {
                        10, 254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
                        12,  16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
                    };
                    length_counter = length_table[value >> 3];
                }

                // Reset sequencer and envelope
                sequence_position = 0;
                envelope_start_flag = true;
                break;
            }
        }
    };

    PulseChannel pulse1;
    PulseChannel pulse2;

    // ===== TRIANGLE CHANNEL =====
    struct TriangleChannel {
        // Linear counter
        uint8_t linear_counter;
        uint8_t linear_counter_reload;
        bool linear_counter_reload_flag;
        bool linear_counter_control;  // Also length counter halt

        // Timer
        uint16_t timer_period;
        uint16_t timer_counter;

        // Sequencer
        uint8_t sequence_position;  // 0-31

        // Length counter
        uint8_t length_counter;

        // Control
        bool enabled;

        TriangleChannel() : linear_counter(0), linear_counter_reload(0),
            linear_counter_reload_flag(false),
            linear_counter_control(false), timer_period(0),
            timer_counter(0), sequence_position(0),
            length_counter(0), enabled(false) {
        }

        uint8_t get_output() {
            // Silence conditions
            if (!enabled) return 0;
            if (length_counter == 0) return 0;
            if (linear_counter == 0) return 0;

            // Ultrasonic frequencies produce a pop instead of proper triangle
            // Some emulators silence below period 2, but hardware produces DC offset
            if (timer_period < 2) return 7;  // Middle of triangle range

            // Triangle wave sequence (32 steps)
            // Counts up from 0 to 15, then down from 15 to 0
            static const uint8_t triangle_sequence[32] = {
                15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
                 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
            };

            return triangle_sequence[sequence_position];
        }

        void clock_timer() {
            if (timer_counter == 0) {
                timer_counter = timer_period;

                // Only advance sequencer if both counters are non-zero
                if (length_counter > 0 && linear_counter > 0) {
                    sequence_position = (sequence_position + 1) & 31;
                }
            }
            else {
                timer_counter--;
            }
        }

        void clock_linear_counter() {
            if (linear_counter_reload_flag) {
                linear_counter = linear_counter_reload;
            }
            else if (linear_counter > 0) {
                linear_counter--;
            }

            // Clear reload flag unless control flag is set
            if (!linear_counter_control) {
                linear_counter_reload_flag = false;
            }
        }

        void clock_length_counter() {
            // Length counter halt is controlled by linear_counter_control
            if (!linear_counter_control && length_counter > 0) {
                length_counter--;
            }
        }

        void write_register(uint8_t reg, uint8_t value) {
            switch (reg) {
            case 0:  // $4008 - Linear counter
                linear_counter_control = (value & 0x80) != 0;
                linear_counter_reload = value & 0x7F;
                break;

            case 1:  // $4009 - Unused
                break;

            case 2:  // $400A - Timer low
                timer_period = (timer_period & 0xFF00) | value;
                break;

            case 3:  // $400B - Length counter load, timer high
                timer_period = (timer_period & 0x00FF) | ((value & 7) << 8);

                // Load length counter
                if (enabled) {
                    static const uint8_t length_table[32] = {
                        10, 254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
                        12,  16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
                    };
                    length_counter = length_table[value >> 3];
                }

                // Set linear counter reload flag
                linear_counter_reload_flag = true;
                break;
            }
        }
    };

    TriangleChannel triangle;

    // ===== NOISE CHANNEL (PERFECT NES ACCURACY) =====
    struct NoiseChannel {
        // Registers
        bool envelope_loop;           // $400C bit 5
        bool constant_volume;         // $400C bit 4
        uint8_t volume_envelope;      // $400C bits 0-3

        uint8_t length_counter_halt;  // Same as envelope_loop
        bool mode_flag;               // $400E bit 7 (0 = long, 1 = short)
        uint8_t timer_period_index;   // $400E bits 0-3

        // Internal state
        uint16_t timer_period;
        uint16_t timer_counter;
        uint16_t shift_register = 1;  // MUST start at 1
        uint8_t envelope_counter;
        uint8_t envelope_decay;
        bool envelope_start_flag;
        uint8_t length_counter = 0;
        bool enabled = false;

        // Lookup table (NTSC) — EXACT from NESdev
        static constexpr uint16_t period_table[16] = {
            4, 8, 16, 32, 64, 96, 128, 160,
            202, 254, 380, 508, 762, 1016, 2034, 4068
        };

        NoiseChannel() {
            Reset();
        }

        void Reset() {
            envelope_loop = constant_volume = mode_flag = false;
            volume_envelope = timer_period_index = 0;
            timer_counter = 0;
            shift_register = 1;
            envelope_counter = envelope_decay = 0;
            envelope_start_flag = true;
            length_counter = 0;
            enabled = false;
        }

        uint8_t get_output() {
            if (!enabled || length_counter == 0 || (shift_register & 1)) {
                return 0;
            }

            uint8_t volume = constant_volume ? volume_envelope : envelope_decay;
            return volume;
        }

        void clock_timer() {
            if (timer_counter == 0) {
                timer_counter = timer_period;

                // Feedback calculation
                uint16_t feedback;
                if (mode_flag) {
                    // Short mode: bits 0 and 6
                    feedback = ((shift_register >> 0) & 1) ^ ((shift_register >> 6) & 1);
                }
                else {
                    // Long mode: bits 0 and 1
                    feedback = ((shift_register >> 0) & 1) ^ ((shift_register >> 1) & 1);
                }

                // Shift right, insert feedback at bit 14
                shift_register = (shift_register >> 1) | (feedback << 14);
            }
            else {
                timer_counter--;
            }
        }

        void clock_envelope() {
            if (envelope_start_flag) {
                envelope_start_flag = false;
                envelope_decay = 15;
                envelope_counter = volume_envelope;
            }
            else if (envelope_counter > 0) {
                envelope_counter--;
            }
            else {
                envelope_counter = volume_envelope;
                if (envelope_decay > 0) {
                    envelope_decay--;
                }
                else if (envelope_loop) {
                    envelope_decay = 15;
                }
            }
        }

        void clock_length_counter() {
            if (!envelope_loop && length_counter > 0) {
                length_counter--;
            }
        }

        void write_register(uint8_t reg, uint8_t value) {
            switch (reg) {
            case 0: // $400C
                envelope_loop = length_counter_halt = (value & 0x20) != 0;
                constant_volume = (value & 0x10) != 0;
                volume_envelope = value & 0x0F;
                break;

            case 2: // $400E
                mode_flag = (value & 0x80) != 0;
                timer_period_index = value & 0x0F;
                timer_period = period_table[timer_period_index];
                break;

            case 3: // $400F
                if (enabled) {
                    static constexpr uint8_t length_table[32] = {
                        10,254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
                        12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
                    };
                    length_counter = length_table[value >> 3];
                }
                envelope_start_flag = true;
                break;
            }
        }
    };

    NoiseChannel noise;

    // ===== DMC (Delta Modulation Channel) =====
    struct DMCChannel {
        // IRQ
        bool irq_enabled;
        bool irq_flag;

        // Loop flag
        bool loop;

        // Timer
        uint16_t timer_period;
        uint16_t timer_counter;

        // Sample
        uint16_t sample_address;
        uint16_t sample_length;
        uint16_t current_address;
        uint16_t bytes_remaining;

        // Output
        uint8_t output_level;  // 7-bit (0-127)
        uint8_t sample_buffer;
        bool sample_buffer_empty;

        // Shift register
        uint8_t shift_register;
        uint8_t bits_remaining;
        bool silence_flag;

        // Control
        bool enabled;

        // Memory read callback (must be set by APU)
        std::function<uint8_t(uint16_t)> read_memory;

        DMCChannel() : irq_enabled(false), irq_flag(false), loop(false),
            timer_period(0), timer_counter(0), sample_address(0),
            sample_length(0), current_address(0), bytes_remaining(0),
            output_level(0), sample_buffer(0), sample_buffer_empty(true),
            shift_register(0), bits_remaining(0), silence_flag(true),
            enabled(false), read_memory(nullptr) {
        }

        uint8_t get_output() {
            // DMC output is 7-bit (0-127)
            return output_level;
        }

        void clock_timer() {
            if (timer_counter == 0) {
                timer_counter = timer_period;

                // Clock the output unit
                if (!silence_flag) {
                    // Check bit 0 of shift register
                    if (shift_register & 1) {
                        // Increment output level
                        if (output_level <= 125) {
                            output_level += 2;
                        }
                    }
                    else {
                        // Decrement output level
                        if (output_level >= 2) {
                            output_level -= 2;
                        }
                    }
                }

                // Shift the register
                shift_register >>= 1;
                bits_remaining--;

                // Check if we need a new byte
                if (bits_remaining == 0) {
                    bits_remaining = 8;

                    if (sample_buffer_empty) {
                        silence_flag = true;
                    }
                    else {
                        silence_flag = false;
                        shift_register = sample_buffer;
                        sample_buffer_empty = true;

                        // Try to fill the buffer
                        fill_sample_buffer();
                    }
                }
            }
            else {
                timer_counter--;
            }
        }

        void fill_sample_buffer() {
            if (sample_buffer_empty && bytes_remaining > 0) {
                // Read sample byte from memory
                if (read_memory) {
                    sample_buffer = read_memory(current_address);
                    sample_buffer_empty = false;
                }

                // Increment address with wrapping
                current_address++;
                if (current_address == 0x0000) {
                    current_address = 0x8000;  // Wrap to start of ROM
                }

                bytes_remaining--;

                // Check if sample is complete
                if (bytes_remaining == 0) {
                    if (loop) {
                        start_sample();
                    }
                    else if (irq_enabled) {
                        irq_flag = true;
                    }
                }
            }
        }

        void start_sample() {
            current_address = sample_address;
            bytes_remaining = sample_length;

            // Fill buffer immediately if empty
            if (sample_buffer_empty && bytes_remaining > 0) {
                fill_sample_buffer();
            }
        }

        void write_register(uint8_t reg, uint8_t value) {
            switch (reg) {
            case 0:  // $4010 - Flags and rate
                irq_enabled = (value & 0x80) != 0;
                loop = (value & 0x40) != 0;

                // Clear IRQ flag if IRQ is disabled
                if (!irq_enabled) {
                    irq_flag = false;
                }

                timer_period = dmc_rate_table[value & 0x0F];
                break;

            case 1:  // $4011 - Direct load
                // Directly set output level (7-bit)
                output_level = value & 0x7F;
                break;

            case 2:  // $4012 - Sample address
                // Sample address = %11AAAAAA.AA000000 = $C000 + (value * 64)
                sample_address = 0xC000 + (value * 64);
                break;

            case 3:  // $4013 - Sample length
                // Sample length = %0000LLLL.LLLL0001 = (value * 16) + 1
                sample_length = (value * 16) + 1;
                break;
            }
        }

        void set_enabled(bool enable) {
            enabled = enable;

            if (!enabled) {
                // Clear bytes remaining
                bytes_remaining = 0;
            }
            else {
                // Start sample if bytes remaining is 0
                if (bytes_remaining == 0) {
                    start_sample();
                }
            }
        }

        bool get_irq_flag() const {
            return irq_flag;
        }

        void clear_irq_flag() {
            irq_flag = false;
        }

        uint16_t get_bytes_remaining() const {
            return bytes_remaining;
        }
    };

    DMCChannel dmc;

    // ===== FRAME COUNTER =====
    struct FrameCounter {
        uint8_t mode;  // 0 = 4-step, 1 = 5-step
        bool irq_inhibit;
        bool irq_flag;
        uint16_t counter;

        void write_control(uint8_t value);
        void step();
    };

    FrameCounter frame_counter;

    // ===== LOOKUP TABLES =====
    static const std::array<uint8_t, 32> length_table;
    // DMC rate table (NTSC)
    static inline const uint16_t dmc_rate_table[16] = {
        428, 380, 340, 320, 286, 254, 226, 214,
        190, 160, 142, 128, 106,  84,  72,  54
    };
    // Noise period lookup table (NTSC)
    static inline const std::array<uint16_t, 16> noise_period_table = {
        4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
    };
    
    static const std::array<uint8_t, 4> pulse_duty_table[4];
    static const std::array<uint8_t, 32> triangle_sequence;

    // ===== HELPER FUNCTIONS =====
    void clock_quarter_frame() {
        // Envelopes & triangle linear counter
        pulse1.clock_envelope();
        pulse2.clock_envelope();
        noise.clock_envelope();
        triangle.clock_linear_counter();
    }

    void clock_half_frame() {
        // Length counters & sweep units
        pulse1.clock_length_counter();
        pulse2.clock_length_counter();
        triangle.clock_length_counter();
        noise.clock_length_counter();

        pulse1.clock_sweep(true);   // Pulse 1 uses one's complement
        pulse2.clock_sweep(false);  // Pulse 2 uses two's complement
    }

    // Audio mixing using non-linear formulas to approximate NES hardware
    float mix_output() {
        // Get channel outputs
        uint8_t p1 = pulse1.get_output();
        uint8_t p2 = pulse2.get_output();
        uint8_t tr = triangle.get_output();
        uint8_t no = noise.get_output();
        uint8_t dm = dmc.get_output();

        // Pulse mixing (non-linear) - RETAIN THIS
        float pulse_out = 0.0f;
        uint8_t pulse_sum = p1 + p2;
        if (pulse_sum > 0) {
            pulse_out = 95.88f / ((8128.0f / pulse_sum) + 100.0f);
        }

        // TND mixing (non-linear) - CRITICAL FIX HERE
        float tnd_out = 0.0f;

        // Use the standard NESdev coefficients for TND sum
        float tr_vol = static_cast<float>(tr) / 8227.0;
        float no_vol = static_cast<float>(no) / 12241.0;
        float dm_vol = static_cast<float>(dm) / 22638.0;

        float tnd_sum = tr_vol + no_vol + dm_vol;

        if (tnd_sum > 0) {
            tnd_out = 159.79f / (1.0f / tnd_sum + 100.0f);
        }

        // Combine and normalize (your output scaling factor might need adjustment based on final taste)
        float output = pulse_out + tnd_out;

        return output * 1.3f; // The scaling factor 1.3f is for final volume adjustment
    }

    struct PulseChannelState {
        // Envelope
        bool envelope_start_flag;
        uint8_t envelope_divider;
        uint8_t envelope_decay_level;
        bool envelope_loop;  // Also doubles as length counter halt
        uint8_t envelope_period;
        bool constant_volume;
        uint8_t constant_volume_period;

        // Sweep unit
        bool sweep_enabled;
        uint8_t sweep_divider;
        uint8_t sweep_period;
        bool sweep_negate;
        uint8_t sweep_shift;
        bool sweep_reload;

        // Timer
        uint16_t timer_period;
        uint16_t timer_counter;

        // Sequencer
        uint8_t duty_cycle;  // 0-3
        uint8_t sequence_position;  // 0-7

        // Length counter
        uint8_t length_counter;

        // Control
        bool enabled;
    };

    struct TriangleChannelState {
        // Linear counter
        uint8_t linear_counter;
        uint8_t linear_counter_reload;
        bool linear_counter_reload_flag;
        bool linear_counter_control;  // Also length counter halt
        // Timer
        uint16_t timer_period;
        uint16_t timer_counter;
        // Sequencer
        uint8_t sequence_position;  // 0-31
        // Length counter
        uint8_t length_counter;
        // Control
        bool enabled;
	};

    struct NoiseChannelState {
        // Registers
        bool envelope_loop;           
        bool constant_volume;         
        uint8_t volume_envelope;      
        uint8_t length_counter_halt;  
        bool mode_flag;               
        uint8_t timer_period_index;   
        // Internal state
        uint16_t timer_period;
        uint16_t timer_counter;
        uint16_t shift_register;
        uint8_t envelope_counter;
        uint8_t envelope_decay;
        bool envelope_start_flag;
        uint8_t length_counter;
		bool enabled;
    };

    struct DMCChannelState {
        // IRQ
        bool irq_enabled;
        bool irq_flag;
        // Loop flag
        bool loop;
        // Timer
        uint16_t timer_period;
        uint16_t timer_counter;
        // Sample
        uint16_t sample_address;
        uint16_t sample_length;
        uint16_t current_address;
        uint16_t bytes_remaining;
        // Output
        uint8_t output_level;  // 7-bit (0-127)
        uint8_t sample_buffer;
        bool sample_buffer_empty;
        // Shift register
        uint8_t shift_register;
        uint8_t bits_remaining;
        bool silence_flag;
        // Control
		bool enabled;
    };

    struct APUState {
        // Frame counter state
        uint8_t frame_counter_mode;  
        bool frame_counter_irq_inhibit;
        bool frame_counter_irq_flag;
        uint8_t frame_counter_step;
        uint8_t frame_counter_reset_delay;
		uint32_t cycle_counter;
    };


};

// ===== REGISTER MAP =====
// $4000-$4003: Pulse 1
// $4004-$4007: Pulse 2
// $4008-$400B: Triangle
// $400C-$400F: Noise
// $4010-$4013: DMC
// $4015: Status (read/write)
// $4017: Frame Counter

// ===== IMPLEMENTATION NOTES =====
/*
APU runs at CPU clock rate (approximately 1.79 MHz NTSC)

Frame Counter modes:
- 4-step: Quarter frame at cycles 3728.5, 7456.5, 11185.5, 14914.5
          Half frame at cycles 7456.5, 14914.5
          IRQ at 14914.5 (if enabled)
- 5-step: Quarter frame at cycles 3728.5, 7456.5, 11185.5, 18640.5
          Half frame at cycles 7456.5, 18640.5
          No IRQ

Audio output mixing (approximate):
pulse_out = 0.00752 * (pulse1 + pulse2)
tnd_out = 0.00851 * triangle + 0.00494 * noise + 0.00335 * dmc
output = pulse_out + tnd_out

Each channel's timer is clocked every APU cycle.
Envelopes, length counters, and sweep units are clocked by frame counter.
*/