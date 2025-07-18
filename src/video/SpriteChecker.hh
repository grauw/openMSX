#ifndef SPRITECHECKER_HH
#define SPRITECHECKER_HH

#include "VDP.hh"

#include "VDPVRAM.hh"
#include "VRAMObserver.hh"
#include "DisplayMode.hh"

#include "narrow.hh"
#include "ranges.hh"
#include "serialize_meta.hh"
#include "unreachable.hh"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace openmsx {

class RenderSettings;
class BooleanSetting;

class SpriteChecker final : public VRAMObserver
{
public:
	/** Bitmap of length 32 describing a sprite pattern.
	  * Visible pixels are 1, transparent pixels are 0.
	  * If the sprite is less than 32 pixels wide,
	  * the lower bits are unused.
	  */
	using SpritePattern = uint32_t;

	/** Contains all the information to draw a line of a sprite.
	  */
	struct SpriteInfo {
		/** Pattern of this sprite line, corrected for magnification.
		  */
		SpritePattern pattern;
		/** X-coordinate of sprite, corrected for early clock.
		  */
		int16_t x;
		/** Bit 3..0 are index in palette.
		  * Bit 6 is 0 for sprite mode 1 like behaviour,
		  * or 1 for OR-ing of sprite colors.
		  * Other bits are undefined.
		  */
		uint8_t colorAttrib;
	};

	static constexpr SpritePattern doublePattern(SpritePattern a)
	{
		// bit-pattern "abcd...." gets expanded to "aabbccdd"
		// upper 16 bits (of a 32 bit number) contain the pattern
		// lower 16 bits must be zero
		//                               // abcdefghijklmnop0000000000000000
		a = (a | (a >> 8)) & 0xFF00FF00; // abcdefgh00000000ijklmnop00000000
		a = (a | (a >> 4)) & 0xF0F0F0F0; // abcd0000efgh0000ijkl0000mnop0000
		a = (a | (a >> 2)) & 0xCCCCCCCC; // ab00cd00ef00gh00ij00kl00mn00op00
		a = (a | (a >> 1)) & 0xAAAAAAAA; // a0b0c0d0e0f0g0h0i0j0k0l0m0n0o0p0
		return a | (a >> 1);             // aabbccddeeffgghhiijjkkllmmnnoopp
	}

	/** Create a sprite checker.
	  * @param vdp The VDP this sprite checker is part of.
	  * @param renderSettings TODO
	  * @param time TODO
	  */
	SpriteChecker(VDP& vdp, RenderSettings& renderSettings,
	              EmuTime time);

	/** Puts the sprite checker in its initial state.
	  * @param time The moment in time this reset occurs.
	  */
	void reset(EmuTime time);

	/** Update sprite checking to specified time.
	  * This includes a VRAM sync.
	  * @param time The moment in emulated time to update to.
	  */
	void sync(EmuTime time) {
		if (!updateSpritesMethod) {
			// Optimization: skip vram sync and sprite checks
			// in sprite mode 0.
			return;
		}
		// Debug:
		// This method is not re-entrant, so check explicitly that it is not
		// re-entered. This can disappear once the VDP-internal scheduling
		// has become stable.
		#ifdef DEBUG
		static bool syncInProgress = false;
		assert(!syncInProgress);
		syncInProgress = true;
		#endif
		vram.sync(time);
		checkUntil(time);
		#ifdef DEBUG
		syncInProgress = false;
		#endif
	}

	/** Clear status bits triggered by reading of S#0.
	  */
	void resetStatus() {
		// TODO: Used to be 0x5F, but that is contradicted by
		//       TMS9918.pdf. Check on real MSX.
		vdp.setSpriteStatus(vdp.getStatusReg0() & 0x1F);
	}

	/** Informs the sprite checker of a VDP display mode change.
	  * @param mode The new display mode.
	  * @param time The moment in emulated time this change occurs.
	  */
	void updateDisplayMode(DisplayMode mode, EmuTime time) {
		sync(time);
		setDisplayMode(mode);

		// The following is only required when switching from sprite
		// mode0 to some other mode (in other case it has no effect).
		// Because in mode 0, currentLine is not updated.
		currentLine = narrow<int>(frameStartTime.getTicksTill_fast(time)
		                 / VDP::TICKS_PER_LINE);
		// Every line in mode0 has 0 sprites, but none of the lines
		// are ever requested by the renderer, except for the last
		// line, because sprites are checked one line before they
		// are displayed. Though frameStart() already makes sure
		// spriteCount contains zero for all lines.
		//   spriteCount[currentLine - 1] = 0;
	}

	/** Informs the sprite checker of a VDP display enabled change.
	  * @param enabled The new display enabled state.
	  * @param time The moment in emulated time this change occurs.
	  */
	void updateDisplayEnabled(bool enabled, EmuTime time) {
		(void)enabled;
		sync(time);
		// TODO: Speed up sprite checking in display disabled case.
	}

	/** Informs the sprite checker of sprite enable changes.
	  * @param enabled The new sprite enabled state.
	  * @param time The moment in emulated time this change occurs.
	  */
	void updateSpritesEnabled(bool enabled, EmuTime time) {
		(void)enabled;
		sync(time);
		// TODO: Speed up sprite checking in display disabled case.
	}

	/** Informs the sprite checker of sprite size or magnification changes.
	  * @param sizeMag The new size and magnification state.
	  *   Bit 0 is magnification: 0 = normal, 1 = doubled.
	  *   Bit 1 is size: 0 = 8x8, 1 = 16x16.
	  * @param time The moment in emulated time this change occurs.
	  */
	void updateSpriteSizeMag(uint8_t sizeMag, EmuTime time) {
		(void)sizeMag;
		sync(time);
		// TODO: Precalc something?
	}

	/** Informs the sprite checker of a change in the TP bit (R#8 bit 5)
	  * @param tp The new transparency value.
	  * @param time The moment in emulated time this change occurs.
	  */
	void updateTransparency(bool tp, EmuTime time) {
		(void)tp;
		sync(time);
	}

	/** Informs the sprite checker of a vertical scroll change.
	  * @param scroll The new scroll value.
	  * @param time The moment in emulated time this change occurs.
	  */
	void updateVerticalScroll(int scroll, EmuTime time) {
		(void)scroll;
		sync(time);
		// TODO: Precalc something?
	}

	/** Update sprite checking until specified line.
	  * VRAM must be up-to-date before this method is called.
	  * It is not allowed to call this method in a spriteless display mode.
	  * @param time The moment in emulated time to update to.
	  */
	void checkUntil(EmuTime time) {
		// TODO:
		// Currently the sprite checking is done atomically at the end of
		// the display line. In reality, sprite checking is probably done
		// during most of the line. Run tests on real MSX to make a more
		// accurate model of sprite checking.
		int limit = narrow<int>(frameStartTime.getTicksTill_fast(time)
		               / VDP::TICKS_PER_LINE);
		if (currentLine < limit) {
			// Call the right update method for the current display mode.
			(this->*updateSpritesMethod)(limit);
		}
	}

	/** Get X coordinate of sprite collision.
	  */
	[[nodiscard]] int getCollisionX(EmuTime time) {
		sync(time);
		return collisionX;
	}

	/** Get Y coordinate of sprite collision.
	  */
	[[nodiscard]] int getCollisionY(EmuTime time) {
		sync(time);
		return collisionY;
	}

	/** Reset sprite collision coordinates.
	  * This happens directly after a read, so a timestamp for syncing is
	  * not necessary.
	  */
	void resetCollision() {
		collisionX = collisionY = 0;
	}

	/** Signals the start of a new frame.
	  * @param time Moment in emulated time the new frame starts.
	  */
	void frameStart(EmuTime time) {
		frameStartTime.reset(time);
		currentLine = 0;
		std::ranges::fill(spriteCount, 0);
		// TODO: Reset anything else? Does the real VDP?
	}

	/** Signals the end of the current frame.
	  * @param time Moment in emulated time the current frame ends.
	  */
	void frameEnd(EmuTime time) {
		sync(time);
	}

	/** Get sprites for a display line.
	  * Returns the contents of the line the last time it was sprite checked;
	  * before getting the sprites, you should sync to a moment in time
	  * after the sprites are checked, or you'll get last frame's sprites.
	  * @param line The absolute line number for which sprites should
	  *   be returned. Range is [0..313) for PAL and [0..262) for NTSC.
	  * @return The to-be-displayed sprites.
	  *   The buffers content remains valid until the next time the VDP
	  *   is scheduled.
	  *   This buffer is followed by a sentinel, this sentinel is NOT
	  *   included in the returned span (IOW the span can be extended with
	  *   one extra element which is the sentinel).
	  */
	[[nodiscard]] std::span<const SpriteInfo> getSprites(int line) const {
		// Compensate for the fact sprites are checked one line earlier
		// than they are displayed.
		line--;

		// TODO: Is there ever a sprite on absolute line 0?
		//       Maybe there is, but it is never displayed.
		if (line < 0) return {};

		return subspan(spriteBuffer[line], 0, spriteCount[line]);
	}

	// VRAMObserver implementation:

	void updateVRAM(unsigned /*offset*/, EmuTime time) override {
		checkUntil(time);
	}

	void updateWindow(bool /*enabled*/, EmuTime time) override {
		sync(time);
	}

	template<typename Archive>
	void serialize(Archive& ar, unsigned version);

private:
	/** Calculate 'updateSpritesMethod' and 'planar'.
	  */
	void setDisplayMode(DisplayMode mode) {
		switch (mode.getSpriteMode(vdp.isMSX1VDP())) {
		case 0:
			updateSpritesMethod = nullptr;
			break;
		case 1:
			updateSpritesMethod = &SpriteChecker::updateSprites1;
			break;
		case 2:
			updateSpritesMethod = &SpriteChecker::updateSprites2;
			planar = mode.isPlanar();
			// An alternative is to have a planar and non-planar
			// updateSprites2 method.
			break;
		default:
			UNREACHABLE;
		}
	}

	/** Calculate sprite patterns for sprite mode 1.
	  */
	void updateSprites1(int limit);

	/** Calculate sprite patterns for sprite mode 2.
	  */
	void updateSprites2(int limit);

	/** Calculates a sprite pattern.
	  * @param patternNr Number of the sprite pattern [0..255].
	  *   For 16x16 sprites, patternNr should be a multiple of 4.
	  * @param y The line number within the sprite: 0 <= y < size.
	  * @return A bit field of the sprite pattern.
	  *   Bit 31 is the leftmost bit of the sprite.
	  *   Unused bits are zero.
	  */
	[[nodiscard]] SpritePattern calculatePatternNP(unsigned patternNr, unsigned y) const;
	[[nodiscard]] SpritePattern calculatePatternPlanar(unsigned patternNr, unsigned y) const;

	/** Check sprite collision and number of sprites per line.
	  * This routine implements sprite mode 1 (MSX1).
	  * Separated from display code to make MSX behaviour consistent
	  * no matter how displaying is handled.
	  * @param minLine The first line number (inclusive) for which sprites
	  *                should be checked.
	  * @param maxLine The last line number (exclusive) for which sprites
	  *                should be checked.
	  * @effect Fills in the spriteBuffer and spriteCount arrays.
	  */
	void checkSprites1(int minLine, int maxLine);

	/** Check sprite collision and number of sprites per line.
	  * This routine implements sprite mode 2 (MSX2).
	  * Separated from display code to make MSX behaviour consistent
	  * no matter how displaying is handled.
	  * @param minLine The first line number (inclusive) for which sprites
	  *                should be checked.
	  * @param maxLine The last line number (exclusive) for which sprites
	  *                should be checked.
	  * @effect Fills in the spriteBuffer and spriteCount arrays.
	  */
	void checkSprites2(int minLine, int maxLine);

private:
	using UpdateSpritesMethod = void (SpriteChecker::*)(int limit);
	UpdateSpritesMethod updateSpritesMethod;

	/** The VDP this sprite checker is part of.
	  */
	VDP& vdp;

	/** The VRAM to get sprites data from.
	  */
	VDPVRAM& vram;

	/** Limit number of sprites per display line?
	  * Option only affects display, not MSX state.
	  * In other words: when false there is no limit to the number of
	  * sprites drawn, but the status register acts like the usual limit
	  * is still effective.
	  */
	BooleanSetting& limitSpritesSetting;

	/** The emulation time when this frame was started (vsync).
	  */
	Clock<VDP::TICKS_PER_SECOND> frameStartTime;

	/** Sprites are checked up to and excluding this display line.
	  */
	int currentLine;

	/** X coordinate of sprite collision.
	  * 9 bits long -> [0..511]?
	  */
	int collisionX;

	/** Y coordinate of sprite collision.
	  * 9 bits long -> [0..511]?
	  * Bit 9 contains EO, I guess that's a copy of the even/odd flag
	  * of the frame on which the collision occurred.
	  */
	int collisionY;

	/** Buffer containing the sprites that are visible on each
	  * display line.
	  */
	std::array<std::array<SpriteInfo, 32 + 1>, 313> spriteBuffer; // +1 for sentinel

	/** Buffer containing the number of sprites that are visible
	  * on each display line.
	  * In other words, spriteCount[i] is the number of sprites
	  * in spriteBuffer[i].
	  */
	std::array<uint8_t, 313> spriteCount;

	/** Is current display mode planar or not?
	  * TODO: Introduce separate update methods for planar/non-planar modes.
	  */
	bool planar;
};
SERIALIZE_CLASS_VERSION(SpriteChecker, 2);

} // namespace openmsx

#endif
