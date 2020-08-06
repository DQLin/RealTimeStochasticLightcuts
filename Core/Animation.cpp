#include "pch.h"
#include "Animation.h"
#include <glm/gtx/transform.hpp>

glm::mat4 Animation::interpolate(const Keyframe& start, const Keyframe& end, double curTime) const
{
	double localTime = curTime - start.time;
	double keyframeDuration = end.time - start.time;
	if (keyframeDuration < 0) keyframeDuration += mDurationInSeconds;
	float factor = keyframeDuration != 0 ? (float)(localTime / keyframeDuration) : 1;

	glm::vec3 translation = mix(start.translation, end.translation, factor);
	glm::vec3 scaling = mix(start.scaling, end.scaling, factor);
	glm::quat rotation = slerp(start.rotation, end.rotation, factor);

	glm::mat4 T;
	T[3] = glm::vec4(translation, 1);
	glm::mat4 R = glm::mat4_cast(rotation);
	glm::mat4 S = glm::scale(scaling);
	glm::mat4 transform = T * R * S;
	return transform;
}