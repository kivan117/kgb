#include "Apu.h"
#include <iostream>

extern void audio_callback(void* user, Uint8* stream, int len);

Apu::Apu()
{
	audio_spec.freq = 2097152;
	audio_spec.format = AUDIO_S16;
	audio_spec.channels = 2;
	audio_stream = SDL_NewAudioStream(AUDIO_S16, 2, 2097152, AUDIO_S16, 2, 48000);
}

void Apu::Update(uint64_t tcycles, bool doubleSpeedMode)
{
	FrameSeqTick(tcycles);

	static int16_t buffer[2];

	for (int64_t i = 0; i < tcycles; i += 4)
	{
		uint16_t updateLength = doubleSpeedMode ? 2 : 4;

		int16_t channel_out[4] = {0,0,0,0};
		
		channel_out[0] = UpdateChannelOne(updateLength);
		channel_out[1] = UpdateChannelTwo(updateLength);
		channel_out[2] = UpdateChannelThree(updateLength);
		channel_out[3] = UpdateChannelFour(updateLength);

		buffer[0] = (channel_out[0] * ChannelPan[0])  / 4;
		buffer[0] += (channel_out[1] * ChannelPan[2]) / 4;
		buffer[0] += (channel_out[2] * ChannelPan[4]) / 4;
		buffer[0] += (channel_out[3] * ChannelPan[6]) / 4;
		buffer[0] *= MasterVolume[0] + 1;

		buffer[1]  = (channel_out[0] * ChannelPan[1]) / 4;
		buffer[1] += (channel_out[1] * ChannelPan[3]) / 4;
		buffer[1] += (channel_out[2] * ChannelPan[5]) / 4;		
		buffer[1] += (channel_out[3] * ChannelPan[7]) / 4;
		buffer[1] *= MasterVolume[1] + 1;
		if(doubleSpeedMode)
			SDL_AudioStreamPut(audio_stream, buffer, sizeof(buffer));
		else
		{
			SDL_AudioStreamPut(audio_stream, buffer, sizeof(buffer));
			SDL_AudioStreamPut(audio_stream, buffer, sizeof(buffer)); //push to audio stream twice in normal speed mode
		}

	}

}

void Apu::SetAudioEnable(bool enable)
{
	audio_master_enable = enable;

	if (!audio_master_enable)
	{
		//stop all audio channels from playing
		channel_one.playing = false;
		channel_two.playing = false;
		channel_three.playing = false;
	}
}

uint8_t Apu::GetAudioEnable()
{
	uint8_t channels = 0;

	if (audio_master_enable)
		channels |= 0x80;

	//if (channel_four.playing)
	//	channels |= 0x08;

	if (channel_three.playing)
		channels |= 0x04;

	if (channel_two.playing)
		channels |= 0x02;

	if (channel_one.playing)
		channels |= 0x01;

	return channels;
}

void Apu::SetMasterVolume(uint8_t value)
{
	MasterVolume[0] = ((value & 0x70) >> 4);
	MasterVolume[1] = ((value & 0x07));
}

void Apu::SetPan(uint8_t value)
{
	ChannelPan[0] = (value & 0x01); //channel 1 left
	ChannelPan[1] = (value & 0x10) >> 4; //channel 1 right

	ChannelPan[2] = (value & 0x02) >> 1; //channel 2 left
	ChannelPan[3] = (value & 0x20) >> 5; //channel 2 right

	ChannelPan[4] = (value & 0x04) >> 2; //channel 3 left
	ChannelPan[5] = (value & 0x40) >> 6; //channel 3 right

	ChannelPan[6] = (value & 0x08) >> 3; //channel 4 left
	ChannelPan[7] = (value & 0x80) >> 7; //channel 4 right
}

void Apu::SetWaveRam(uint8_t index, uint8_t value)
{
	if (channel_three.enable)
	{
		WaveRam[(channel_three.pattern_buffer_counter & 0x1E)] = (value & 0xF0) >> 4;
		WaveRam[(channel_three.pattern_buffer_counter & 0x1E) + 1] = (value & 0x0F);
	}
	else
	{
		WaveRam[index * 2] = (value & 0xF0) >> 4;
		WaveRam[(index * 2) + 1] = (value & 0x0F);
	}

	return;
}

//channel one

int16_t Apu::UpdateChannelOne(int16_t tcycles)
{
	if (channel_one.playing)
	{
		for (int i = 0; i < tcycles; i++)
		{
			channel_one.frequency_timer -= 1;
			if (channel_one.frequency_timer == 0)
			{
				channel_one.frequency_timer = (2048 - channel_one.frequency) * 4;

				channel_one.wave_pattern_counter = ++channel_one.wave_pattern_counter % 8;
			}
		}

		int16_t temp = duty_waveforms[(channel_one.wave_pattern * 8) + channel_one.wave_pattern_counter] ? 1 : -1;
		temp *= (256 * channel_one.current_volume);
		return temp;
	}

	return audio_spec.silence;
}

void Apu::ChannelOneTrigger(uint8_t value)
{

	if (value & 0x40)
		channel_one.length_enable = true;
	else
		channel_one.length_enable = false;

	channel_one.frequency = ((uint16_t)(value & 0x07) << 8) | (channel_one.frequency & 0xFF);

	if ((value & 0x80) && audio_master_enable)
	{
		channel_one.playing = true;
		if (channel_one.length_enable && channel_one.length_counter == 0)
			channel_one.length_counter = 0x40;
		channel_one.wave_pattern_counter = 0;
		channel_one.frequency_timer = (2048 - channel_one.frequency) * 4;

		channel_one.current_volume = channel_one.start_volume;
		channel_one.volume_period_counter = channel_one.volume_envelope_period;

		channel_one.shadow_frequency = channel_one.frequency;
		channel_one.sweep_period_counter = channel_one.sweep_period ? channel_one.sweep_period : 8;
		if (channel_one.sweep_shift || channel_one.sweep_period)
			channel_one.sweep_enable = true;
		else
			channel_one.sweep_enable = false;

		if (channel_one.sweep_shift)
		{
			//overflow check again?
			uint16_t new_freq = channel_one.shadow_frequency >> channel_one.sweep_shift;
			if (channel_one.sweep_dir)
				new_freq = channel_one.shadow_frequency - new_freq;
			else
				new_freq = channel_one.shadow_frequency + new_freq;

			if (new_freq > 2047)
				channel_one.playing = false;
		}
	}

}

void Apu::ChannelOneSetLength(uint8_t value)
{
	//Bit 7-6 - Wave Pattern Duty (Read/Write)`
	//Bit 5 - 0 - Sound length data(Write Only) (t1: 0 - 63)`

	channel_one.wave_pattern = (value & 0xC0) >> 6;
	channel_one.length_counter_setpoint = value & 0x3F;
	channel_one.length_counter = 0x40 - channel_one.length_counter_setpoint;
}

void Apu::ChannelOneSetFreq(uint8_t value)
{
	channel_one.frequency = (channel_one.frequency & 0x700) | value;
}

void Apu::ChannelOneSetVolume(uint8_t value)
{
	channel_one.start_volume = (value & 0xF0) >> 4;
	channel_one.volume_envelope_dir = (value & 0x08) >> 3;
	channel_one.volume_envelope_period = (value & 0x07);
}

void Apu::ChannelOneSetSweep(uint8_t value)
{
	channel_one.sweep_period = (value & 0x70) >> 4;
	channel_one.sweep_dir = (value & 0x08) >> 3; //0 is sweep up, 1 is sweep down
	channel_one.sweep_shift = (value & 0x07); //amount to shift shadow reg
}

//channel two

int16_t Apu::UpdateChannelTwo(int16_t tcycles)
{
	if (channel_two.playing)
	{
		for (int i = 0; i < tcycles; i++)
		{
			channel_two.frequency_timer -= 1;
			if (channel_two.frequency_timer == 0)
			{
				channel_two.frequency_timer = (2048 - channel_two.frequency) * 4;

				channel_two.wave_pattern_counter = ++channel_two.wave_pattern_counter % 8;
			}
		}

		int16_t temp = duty_waveforms[(channel_two.wave_pattern * 8) + channel_two.wave_pattern_counter] ? 1 : -1;
		temp *= (256 * channel_two.current_volume);
		return temp;
	}

	return audio_spec.silence;
}

void Apu::ChannelTwoTrigger(uint8_t value)
{
	//Bit 7 - Initial(1 = Restart Sound)     (Write Only)`
	//Bit 6 - Counter / consecutive selection(Read / Write)`
	//(1 = Stop output when length in NR21 expires)`
	//Bit 2 - 0 - Frequency's higher 3 bits (x) (Write Only)`

	if (value & 0x40)
		channel_two.length_enable = true;
	else
		channel_two.length_enable = false;

	channel_two.frequency = ((uint16_t)(value & 0x07) << 8) | (channel_two.frequency & 0xFF);

	if ((value & 0x80) && audio_master_enable)
	{
		channel_two.playing = true;
		if (channel_two.length_enable && channel_two.length_counter == 0)
			channel_two.length_counter = 0x40;
		channel_two.wave_pattern_counter = 0;
		channel_two.frequency_timer = (2048 - channel_two.frequency) * 4;

		channel_two.current_volume = channel_two.start_volume;
		channel_two.volume_period_counter = channel_two.volume_envelope_period;
	}

}

void Apu::ChannelTwoSetLength(uint8_t value)
{
	//Bit 7-6 - Wave Pattern Duty (Read/Write)`
	//Bit 5 - 0 - Sound length data(Write Only) (t1: 0 - 63)`

	channel_two.wave_pattern = (value & 0xC0) >> 6;
	channel_two.length_counter_setpoint = value & 0x3F;
	channel_two.length_counter = 0x40 - channel_two.length_counter_setpoint;
}

void Apu::ChannelTwoSetFreq(uint8_t value)
{
	channel_two.frequency = (channel_two.frequency & 0x700) | value;
}

void Apu::ChannelTwoSetVolume(uint8_t value)
{
	channel_two.start_volume = (value & 0xF0) >> 4;
	channel_two.volume_envelope_dir = (value & 0x08) >> 3;
	channel_two.volume_envelope_period = (value & 0x07);
}

//channel three

int16_t Apu::UpdateChannelThree(int16_t tcycles)
{
	if (channel_three.playing)
	{
		for (int i = 0; i < tcycles; i++)
		{
			channel_three.frequency_timer -= 1;
			if (channel_three.frequency_timer == 0)
			{
				channel_three.frequency_timer = (2048 - channel_three.frequency) * 2;

				channel_three.pattern_buffer_counter = ++channel_three.pattern_buffer_counter % 32;

				channel_three.pattern_buffer = WaveRam[channel_three.pattern_buffer_counter];
			}
		}

		return ((512 * (channel_three.pattern_buffer >> channel_three.volume_shift)) - 3840);
		//return (channel_three.pattern_buffer >> channel_three.volume_shift);
		////int16_t output = audio_spec.silence;
		//uint8_t index = ((channel_three.pattern_buffer) >> channel_three.volume_shift);
		////if (index != 0)
		//return output;
	}
	return audio_spec.silence;
}

void Apu::ChannelThreeTrigger(uint8_t value)
{
	//  Bit 7 - Initial(1 = Restart Sound)     (Write Only)`
	//	Bit 6 - Counter / consecutive selection(Read / Write)`
	//	(1 = Stop output when length in NR31 expires)`
	//	Bit 2 - 0 - Frequency's higher 3 bits (x) (Write Only)`

	if (value & 0x40)
		channel_three.length_enable = true;
	else
		channel_three.length_enable = false;

	channel_three.frequency = ((uint16_t)(value & 0x07) << 8) | (channel_three.frequency & 0xFF);

	if ((value & 0x80) && audio_master_enable)
	{
		channel_three.playing = true;
		if (channel_three.length_counter == 0)
			channel_three.length_counter = 0x100;
		channel_three.pattern_buffer_counter = 0;

		//channel_three.pattern_buffer = WaveRam[channel_three.pattern_buffer_counter];

		channel_three.frequency_timer = (2048 - channel_three.frequency) * 2;
	}

	if(!channel_three.enable)
		channel_three.playing = false;
}

void Apu::ChannelThreeSetEnable(uint8_t value)
{
	if (value & 0x80)
	{
		channel_three.enable = true;
		if (audio_master_enable)
		{
			channel_three.playing = true;
			if (channel_three.length_counter == 0)
				channel_three.length_counter = 0x100;
			channel_three.pattern_buffer_counter = 0;

			//channel_three.pattern_buffer = WaveRam[channel_three.pattern_buffer_counter];

			channel_three.frequency_timer = (2048 - channel_three.frequency) * 2;
		}
	}
	else
	{
		channel_three.enable = false;
		channel_three.playing = false;
	}
}

void Apu::ChannelThreeSetLength(uint8_t value)
{
	channel_three.length_counter_setpoint = value;
	channel_three.length_counter = channel_three.length_counter_setpoint;
	//channel_three.length_counter = 0x100 - channel_three.length_counter_setpoint;
}

void Apu::ChannelThreeSetFreq(uint8_t value)
{
	channel_three.frequency = (channel_three.frequency & 0x700) | value;
}

void Apu::ChannelThreeSetVolume(uint8_t value)
{
	uint8_t vol_setting = (value & 0x60) >> 5;
	switch (vol_setting)
	{
	case(0):
		channel_three.volume_shift = 4;
		break;
	case(1):
		channel_three.volume_shift = 0;
		break;
	case(2):
		channel_three.volume_shift = 1;
		break;
	case(3):
		channel_three.volume_shift = 2;
		break;
	default:
		break;
	}
}

// channel four

int16_t Apu::UpdateChannelFour(int16_t tcycles)
{
	if (channel_four.playing)
	{
		for (int i = 0; i < tcycles; i++)
		{
			channel_four.frequency_timer -= 1;
			if (channel_four.frequency_timer == 0)
			{
				channel_four.frequency_timer = channel_four.divisor << channel_four.divisor_shift;

				uint8_t result = (channel_four.lfsr & 0x01) ^ ((channel_four.lfsr >> 1) & 0x01);
				channel_four.lfsr = (channel_four.lfsr >> 1) | (result << 14);

				if (channel_four.width_mode)
				{
					channel_four.lfsr &= 0xFFBF; //clear bit 6
					channel_four.lfsr |= (result << 6);
				}
			}
		}

		int16_t temp = (channel_four.lfsr & 0x01) ? -1 : 1;
		temp *= (256 * channel_four.current_volume);
		return temp;
	}

	return audio_spec.silence;
}

void Apu::ChannelFourTrigger(uint8_t value)
{
	if (value & 0x40)
		channel_four.length_enable = true;
	else
		channel_four.length_enable = false;

	if (value & 0x80)
	{
		channel_four.playing = true;

		if (channel_four.length_enable && channel_four.length_counter == 0)
			channel_four.length_counter = 0x40;

		channel_four.lfsr = 0xFFFF;
		channel_four.frequency_timer = channel_four.divisor << channel_four.divisor_shift;

		channel_four.current_volume = channel_four.start_volume;
		channel_four.volume_period_counter = channel_four.volume_envelope_period;
	}
}

void Apu::ChannelFourSetLength(uint8_t value)
{
	channel_four.length_counter_setpoint = value & 0x3F;
	channel_four.length_counter = 0x40 - channel_four.length_counter_setpoint;
}

void Apu::ChannelFourSetPoly(uint8_t value)
{
	channel_four.divisor_shift = (value & 0xF0) >> 4;
	channel_four.width_mode = (value & 0x08) >> 3;
	channel_four.divisor = divisor_code[(value & 0x07)];
}

void Apu::ChannelFourSetVolume(uint8_t value)
{
	channel_four.start_volume = (value & 0xF0) >> 4;
	channel_four.volume_envelope_dir = (value & 0x08) >> 3;
	channel_four.volume_envelope_period = (value & 0x07);
}

//Frame sequencer

void Apu::FrameSeqTick(uint16_t tcycles)
{
	fs_cycles += tcycles;

	while (fs_cycles >= 8192) // 4194304hz / 512hz
	{
		fs_cycles -= 8192;
		fs_current_step = ++fs_current_step % 8;
		switch (fs_current_step)
		{
		case(0):
			FrameSeqLengthStep();
			break;
		case(2):
			FrameSeqLengthStep();
			FrameSeqSweepStep();
			break;
		case(4):
			FrameSeqLengthStep();
			break;
		case(6):
			FrameSeqLengthStep();
			FrameSeqSweepStep();
			break;
		case(7):
			FrameSeqVolStep();
			break;
		default:
			break;
		}

	}
}

void Apu::FrameSeqLengthStep()
{
	if (channel_one.playing && channel_one.length_enable)
	{
		channel_one.length_counter--;
		if (channel_one.length_counter == 0)
		{
			channel_one.playing = false;
		}
	}

	if (channel_two.playing && channel_two.length_enable)
	{
		channel_two.length_counter--;
		if (channel_two.length_counter == 0)
		{
			channel_two.playing = false;
		}
	}

	if (channel_three.playing && channel_three.length_enable)
	{
		channel_three.length_counter--;
		if (channel_three.length_counter == 0)
		{
			channel_three.playing = false;
		}
	}

	if (channel_four.playing && channel_four.length_enable)
	{
		channel_four.length_counter--;
		if (channel_four.length_counter == 0)
		{
			channel_four.playing = false;
		}
	}
}

void Apu::FrameSeqVolStep()
{
	if (channel_one.volume_envelope_period != 0)
	{
		channel_one.volume_period_counter--;
		if (channel_one.volume_period_counter == 0)
		{
			channel_one.volume_period_counter = channel_one.volume_envelope_period;
			if (channel_one.volume_envelope_dir)
			{
				if (channel_one.current_volume < 0x0F)
					channel_one.current_volume++;
			}
			else
			{
				if (channel_one.current_volume > 0x00)
					channel_one.current_volume--;
			}
		}
	}
	if (channel_two.volume_envelope_period != 0)
	{
		channel_two.volume_period_counter--;
		if (channel_two.volume_period_counter == 0)
		{
			channel_two.volume_period_counter = channel_two.volume_envelope_period;
			if (channel_two.volume_envelope_dir)
			{
				if (channel_two.current_volume < 0x0F)
					channel_two.current_volume++;
			}
			else
			{
				if (channel_two.current_volume > 0x00)
					channel_two.current_volume--;
			}
		}
	}
	if (channel_four.volume_envelope_period != 0)
	{
		channel_four.volume_period_counter--;
		if (channel_four.volume_period_counter == 0)
		{
			channel_four.volume_period_counter = channel_four.volume_envelope_period;
			if (channel_four.volume_envelope_dir)
			{
				if (channel_four.current_volume < 0x0F)
					channel_four.current_volume++;
			}
			else
			{
				if (channel_four.current_volume > 0x00)
					channel_four.current_volume--;
			}
		}
	}
	return;
}

void Apu::FrameSeqSweepStep()
{
	if (channel_one.sweep_period_counter > 0)
	{
		channel_one.sweep_period_counter--;
		if (channel_one.sweep_period_counter == 0)
		{
			channel_one.sweep_period_counter = channel_one.sweep_period == 0 ? 8 : channel_one.sweep_period;

			if (channel_one.sweep_enable && channel_one.sweep_period)
			{
				uint16_t new_freq = channel_one.shadow_frequency >> channel_one.sweep_shift;
				if (channel_one.sweep_dir)
					new_freq = channel_one.shadow_frequency - new_freq;
				else
					new_freq = channel_one.shadow_frequency + new_freq;
				//overflow check
				if (new_freq > 2047)
					channel_one.playing = false;
				else if (channel_one.sweep_shift)
				{
					channel_one.frequency = new_freq;
					channel_one.shadow_frequency = new_freq;

					//overflow check again?
					new_freq = channel_one.shadow_frequency >> channel_one.sweep_shift;
					if (channel_one.sweep_dir)
						new_freq = channel_one.shadow_frequency - new_freq;
					else
						new_freq = channel_one.shadow_frequency + new_freq;

					if (new_freq > 2047)
						channel_one.playing = false;
				}
			}

		}
	}

	return;
}