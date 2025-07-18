#ifndef SN76489_HH
#define SN76489_HH

#include "ResampledSoundDevice.hh"
#include "SimpleDebuggable.hh"

#include <array>

namespace openmsx {

/** This class implements the Texas Instruments SN76489 sound chip.
  * Unlike the AY-3-8910, this chip only performs sound synthesis.
  *
  * Resources used:
  * - SN76489AN data sheet
  *   http://map.grauw.nl/resources/sound/texas_instruments_sn76489an.pdf
  * - Developer documentation on SMS Power!
  *   http://www.smspower.org/Development/SN76489
  * - John Kortink's reverse engineering results
  *   http://www.zeridajh.org/articles/various/sn76489/index.htm
  * - MAME's implementation
  *   https://github.com/mamedev/mame/blob/master/src/devices/sound/sn76496.cpp
  * - blueMSX's implementation
  *   https://sourceforge.net/p/bluemsx/code/HEAD/tree/trunk/blueMSX/Src/SoundChips/SN76489.c
  */
class SN76489 final : public ResampledSoundDevice
{
public:
	explicit SN76489(const DeviceConfig& config);
	~SN76489();

	// ResampledSoundDevice
	void generateChannels(std::span<float*> buffers, unsigned num) override;

	void reset(EmuTime time);
	void write(uint8_t value, EmuTime time);

	template<typename Archive>
	void serialize(Archive& ar, unsigned version);

private:
	class NoiseShifter {
	public:
		/** Sets the feedback pattern and resets the shift register to the
		  * initial state that matches that pattern.
		  */
		void initState(unsigned pattern, unsigned period);

		/** Gets the current output of this shifter: 0 or 1.
		  */
		[[nodiscard]] unsigned getOutput() const;

		/** Advances the shift register one step, to the next output.
		  */
		void advance();

		/** Records that the shift register should be advanced multiple steps
		  * before the next output is used.
		  */
		void queueAdvance(unsigned steps);

		/** Advances the shift register by the number of steps that were queued.
		  */
		void catchUp();

		template<typename Archive>
		void serialize(Archive& ar, unsigned version);

	private:
		unsigned pattern;
		unsigned period;
		unsigned random;
		unsigned stepsBehind;
	};

	/** Initialize registers, counters and other chip state.
	  */
	void initState();

	/** Initialize noise shift register according to chip variant and noise
	  * mode.
	  */
	void initNoise();

	[[nodiscard]] uint16_t peekRegister(unsigned reg, EmuTime time) const;
	void writeRegister(unsigned reg, uint16_t value, EmuTime time);
	template<bool NOISE> void synthesizeChannel(
		float*& buffer, unsigned num, unsigned generator);

private:
	NoiseShifter noiseShifter;

	/** Register bank. The tone period registers (0, 2, 4) are 12 bits wide,
	  * all other registers are 4 bits wide.
	  */
	std::array<uint16_t, 8> regs;

	/** The last register written to (0-7).
	  */
	uint8_t registerLatch;

	/** Frequency counter for each channel.
	  * These count down and when they reach zero, the output flips and the
	  * counter is re-loaded with the value of the period register.
	  */
	std::array<uint16_t, 4> counters;

	/** Output flip-flop state (0 or 1) for each channel.
	  */
	std::array<uint8_t, 4> outputs;

	struct Debuggable final : SimpleDebuggable {
		Debuggable(MSXMotherBoard& motherBoard, const std::string& name);
		[[nodiscard]] uint8_t read(unsigned address, EmuTime time) override;
		void write(unsigned address, uint8_t value, EmuTime time) override;
	} debuggable;
};

} // namespace openmsx

#endif
