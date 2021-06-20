#pragma once
#include <SDL.h>
#include <stdint.h>
#include <cmath>
#include <array>

class Apu
{
public:
	Apu();
	void Update(uint64_t tcycles, bool doubleSpeedMode);
	SDL_AudioStream* audio_stream;
	void SetAudioEnable(bool enable);
	uint8_t GetAudioEnable();

	void SetMasterVolume(uint8_t value);
	void SetPan(uint8_t value);
	void SetWaveRam(uint8_t index, uint8_t value);

	void ChannelOneTrigger(uint8_t value);
	void ChannelOneSetLength(uint8_t value);
	void ChannelOneSetFreq(uint8_t value);
	void ChannelOneSetVolume(uint8_t value);
	void ChannelOneSetSweep(uint8_t value);

	void ChannelTwoTrigger(uint8_t value);
	void ChannelTwoSetLength(uint8_t value);
	void ChannelTwoSetFreq(uint8_t value);
	void ChannelTwoSetVolume(uint8_t value);

	void ChannelThreeTrigger(uint8_t value);
	void ChannelThreeSetEnable(uint8_t value);
	void ChannelThreeSetLength(uint8_t value);
	void ChannelThreeSetFreq(uint8_t value);
	void ChannelThreeSetVolume(uint8_t value);

	void ChannelFourTrigger(uint8_t value);
	void ChannelFourSetLength(uint8_t value);
	void ChannelFourSetPoly(uint8_t value);
	void ChannelFourSetVolume(uint8_t value);

	void ToggleMute();

private:
	
	uint16_t duty_waveforms[32] = {	0, 0, 0, 0, 0, 0, 1, 0,
									0, 0, 0, 0, 0, 0, 1, 1,
									0, 0, 0, 0, 1, 1, 1, 1,
									1, 1, 1, 1, 1, 1, 0, 0	};

	bool audio_master_enable = true;

	uint16_t fs_cycles = 0;
	uint8_t fs_current_step = 0;

	void FrameSeqTick(uint16_t tcycles);
	void FrameSeqLengthStep();
	void FrameSeqVolStep();
	void FrameSeqSweepStep();

	struct ch_one {
		bool playing{ false };
		uint8_t wave_pattern{ 0 };
		uint8_t wave_pattern_counter{ 0 };
		uint8_t length_counter{ 0 };
		uint8_t length_counter_setpoint{ 0 };
		bool length_enable{ false };
		uint16_t frequency{ 0 };
		uint16_t frequency_timer{ 0 };
		uint8_t start_volume{ 0 };
		uint8_t volume_envelope_dir{ 0 };
		uint8_t volume_envelope_period{ 0 };
		uint8_t current_volume{ 0 };
		uint8_t volume_period_counter{ 0 };
		uint16_t shadow_frequency{ 0 };
		uint8_t sweep_period{ 0 };
		uint8_t sweep_period_counter{ 0 };
		uint8_t sweep_dir{ 0 };
		uint8_t sweep_shift{ 0 };
		bool sweep_enable{ false };

	} channel_one;

	struct ch_two {
		bool playing{ false };
		uint8_t wave_pattern{ 0 };
		uint8_t wave_pattern_counter{ 0 };
		uint8_t length_counter{ 0 };
		uint8_t length_counter_setpoint{ 0 };
		bool length_enable{ false };
		uint16_t frequency{ 0 };
		uint16_t frequency_timer{ 0 };
		uint8_t start_volume{ 0 };
		uint8_t volume_envelope_dir{ 0 };
		uint8_t volume_envelope_period{ 0 };
		uint8_t current_volume{ 0 };
		uint8_t volume_period_counter{ 0 };
	} channel_two;

	struct ch_three {
		bool playing{ false };
		bool enable{ false };
		uint8_t pattern_buffer{ 0 };
		uint8_t pattern_buffer_counter{ 0 };
		uint16_t length_counter{ 0 };
		uint8_t length_counter_setpoint{ 0 };
		bool length_enable{ false };
		uint16_t frequency{ 0 };
		uint16_t frequency_timer{ 0 };
		uint8_t volume_shift{ 0 };
	} channel_three;

	struct ch_four {
		bool playing{ false };
		
		uint16_t lfsr{ 0xFFFF };
		uint8_t divisor{ 0 };
		uint8_t divisor_shift{ 0 };
		uint8_t width_mode{ 0 };

		uint8_t length_counter{ 0 };
		uint8_t length_counter_setpoint{ 0 };
		bool length_enable{ false };
		uint16_t frequency_timer{ 0 };
		uint8_t start_volume{ 0 };
		uint8_t volume_envelope_dir{ 0 };
		uint8_t volume_envelope_period{ 0 };
		uint8_t current_volume{ 0 };
		uint8_t volume_period_counter{ 0 };
	} channel_four;


	int16_t UpdateChannelOne(int16_t tcycles);
	int16_t UpdateChannelTwo(int16_t tcycles);
	int16_t UpdateChannelThree(int16_t tcycles);
	int16_t UpdateChannelFour(int16_t tcycles);

	uint8_t WaveRam[32] = { 0 };
	uint8_t divisor_code[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };

	SDL_AudioSpec audio_spec;

	uint16_t MasterVolume[2] = { 0xFFFF, 0xFFFF };
	uint16_t ChannelPan[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };

	bool MuteAll{ false };
};