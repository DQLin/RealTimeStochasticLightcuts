#pragma once

#include "Animation.h"
#include <glm/gtx/transform.hpp>

class SimpleAnimation : public GeneralAnimation
{
public:

	struct AnimProcedure
	{
		enum struct ProcedureType
		{
			Translation = 0,
			Rotation = 1
		};

		AnimProcedure() { sine_x = glm::vec4(0.f); sine_y = glm::vec4(0.f); sine_z = glm::vec4(0.f); type = ProcedureType::Translation; };

		ProcedureType type; // 0 translation, 1 rotation
		union
		{
			glm::vec4 sine_x;
			glm::vec3 rotation_axis;
		};
		union
		{
			glm::vec4 sine_y;
			float phase;
		};
		union
		{
			glm::vec4 sine_z;
			float w;
		};
	};

	SimpleAnimation(const std::string& name, double durationInSeconds) : GeneralAnimation(name, durationInSeconds) {}

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
	
	void addProcedure(size_t channelID, const AnimProcedure& proc)
	{
		assert(channelID < mChannels.size());
		mChannels[channelID].procedures.push_back(proc);
	}

private:

	struct Channel
	{
		Channel(size_t matID) : matrixID(matID) {};
		size_t matrixID;
		std::vector<AnimProcedure> procedures;
	};

	std::vector<Channel> mChannels;

	virtual glm::mat4 animateChannel(Channel& c, double time)
	{
		glm::mat4 ret = {};
		for (auto& proc : c.procedures)
		{
			if (proc.type == AnimProcedure::ProcedureType::Translation)
			{
				float x = proc.sine_x.x + proc.sine_x.y * sin(proc.sine_x.z * time + proc.sine_x.w);
				float y = proc.sine_y.x + proc.sine_y.y * sin(proc.sine_y.z * time + proc.sine_y.w);
				float z = proc.sine_z.x + proc.sine_z.y * sin(proc.sine_z.z * time + proc.sine_z.w);
				ret = glm::translate(ret, glm::vec3(x,y,z));
			}
			else if (proc.type == AnimProcedure::ProcedureType::Rotation)
			{
				ret = glm::rotate(ret, proc.phase + proc.w * (float)time, proc.rotation_axis);
			}
		}
		return ret;
	}
};
