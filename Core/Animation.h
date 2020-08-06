#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>
#include <string>

class GeneralAnimation
{
public:
	/** Run the animation
	\param currentTime The current time in seconds. This can be larger then the animation time, in which case the animation will loop
	\param matrices The array of global matrices to update
	*/
	virtual void animate(double currentTime, std::vector<glm::mat4>& matrices) = 0;

	const std::string& getName() const { return mName; }

	GeneralAnimation(const std::string& name, double durationInSeconds) : mName(name), mDurationInSeconds(durationInSeconds) {}

protected:

	const std::string mName;
	double mDurationInSeconds = 0;
};

class Animation : public GeneralAnimation
{
public:

	struct Keyframe
	{
		double time = 0;
		glm::vec3 translation = glm::vec3(0, 0, 0);
		glm::vec3 scaling = glm::vec3(1, 1, 1);
		glm::quat rotation = glm::quat(1, 0, 0, 0);
	};

	virtual void animate(double currentTime, std::vector<glm::mat4>& matrices)
	{
		// Calculate the relative time
		double modTime = fmod(currentTime, mDurationInSeconds);

		for (auto& c : mChannels)
		{
			matrices[c.matrixID] = animateChannel(c, modTime);
		}
	};

	/** Add a new channel
	*/
	size_t addChannel(size_t matrixID)
	{
		mChannels.push_back(Channel(matrixID));
		return mChannels.size() - 1;
	}

	/** Get the channel count
	*/
	size_t getChannelCount() const { return mChannels.size(); }

	/** Add a keyframe.
		If there's already a keyframe at the requested time, this call will override the existing frame
	*/
	void addKeyframe(size_t channelID, const Keyframe& keyframe)
	{
		assert(channelID < mChannels.size());
		assert(keyframe.time <= mDurationInSeconds);

		mChannels[channelID].lastKeyframeUsed = 0;
		auto& channelFrames = mChannels[channelID].keyframes;

		if (channelFrames.size() == 0 || channelFrames[0].time > keyframe.time)
		{
			channelFrames.insert(channelFrames.begin(), keyframe);
			return;
		}
		else
		{
			for (size_t i = 0; i < channelFrames.size(); i++)
			{
				auto& current = channelFrames[i];
				// If we already have a key-frame at the same time, replace it
				if (current.time == keyframe.time)
				{
					current = keyframe;
					return;
				}

				// If this is not the last frame, Check if we are in between frames
				if (i < channelFrames.size() - 1)
				{
					auto& Next = channelFrames[i + 1];
					if (current.time < keyframe.time && Next.time > keyframe.time)
					{
						channelFrames.insert(channelFrames.begin() + i + 1, keyframe);
						return;
					}
				}
			}

			// If we got here, need to push it to the end of the list
			channelFrames.push_back(keyframe);
		}
	}

	/** Get the keyframe from a specific time.
		If the keyframe doesn't exists, the function will throw an exception. If you don't want to handle exceptions, call doesKeyframeExist() first
	*/
	const Keyframe& getKeyframe(size_t channelID, double time) const
	{
		assert(channelID < mChannels.size());
		for (const auto& k : mChannels[channelID].keyframes)
		{
			if (k.time == time) return k;
		}
		throw std::runtime_error(("Animation::getKeyframe() - can't find a keyframe at time " + std::to_string(time)).c_str());
	}

	/** Check if a keyframe exists in a specific time
	*/
	bool doesKeyframeExists(size_t channelID, double time) const
	{
		assert(channelID < mChannels.size());
		for (const auto& k : mChannels[channelID].keyframes)
		{
			if (k.time == time) return true;
		}
		return false;
	}

	/** Get the matrixID affected by a channel
	*/
	size_t getChannelMatrixID(size_t channel) const { return mChannels[channel].matrixID; }

	Animation(const std::string& name, double durationInSeconds) : GeneralAnimation(name, durationInSeconds) {}

private:

	struct Channel
	{
		Channel(size_t matID) : matrixID(matID) {};
		size_t matrixID;
		std::vector<Keyframe> keyframes;
		size_t lastKeyframeUsed = 0;
		double lastUpdateTime = 0;
	};

	std::vector<Channel> mChannels;

	glm::mat4 animateChannel(Channel& c, double time)
	{
		size_t curKeyIndex = findChannelFrame(c, time);
		size_t nextKeyIndex = curKeyIndex + 1;
		if (nextKeyIndex == c.keyframes.size()) nextKeyIndex = 0;

		c.lastUpdateTime = time;
		c.lastKeyframeUsed = curKeyIndex;

		return interpolate(c.keyframes[curKeyIndex], c.keyframes[nextKeyIndex], time);
	}

	size_t findChannelFrame(const Channel& c, double time) const
	{
		size_t frameID = (time < c.lastUpdateTime) ? 0 : c.lastKeyframeUsed;
		while (frameID < c.keyframes.size() - 1)
		{
			if (c.keyframes[frameID + 1].time > time) break;
			frameID++;
		}
		return frameID;
	}
	glm::mat4 interpolate(const Keyframe& start, const Keyframe& end, double curTime) const;
};