#include "LDSDLRasterizer.hh"
#include "RawFrame.hh"
#include "PostProcessor.hh"
#include "OutputSurface.hh"
#include "PixelFormat.hh"
#include <cstdint>
#include <memory>

namespace openmsx {

LDSDLRasterizer::LDSDLRasterizer(
		OutputSurface& screen,
		std::unique_ptr<PostProcessor> postProcessor_)
	: postProcessor(std::move(postProcessor_))
	, workFrame(std::make_unique<RawFrame>(screen.getPixelFormat(), 640, 480))
{
}

LDSDLRasterizer::~LDSDLRasterizer() = default;

PostProcessor* LDSDLRasterizer::getPostProcessor() const
{
	return postProcessor.get();
}

void LDSDLRasterizer::frameStart(EmuTime::param time)
{
	workFrame = postProcessor->rotateFrames(std::move(workFrame), time);
}

void LDSDLRasterizer::drawBlank(int r, int g, int b)
{
	// We should really be presenting the "LASERVISION" text
	// here, like the real laserdisc player does. Note that this
	// changes when seeking or starting to play.
	auto background = workFrame->getPixelFormat().map(r, g, b);
	for (auto y : xrange(480)) {
		workFrame->setBlank(y, background);
	}
}

RawFrame* LDSDLRasterizer::getRawFrame()
{
	return workFrame.get();
}

} // namespace openmsx
