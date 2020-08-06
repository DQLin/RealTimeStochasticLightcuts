#include "SLCDemo.h"
#include "tinyxml/tinyxml.h"

#ifdef _WIN32
#define COMPARE(a,b) (_stricmp(a,b)==0)
#else
#define COMPARE(a,b) (strcasecmp(a,b)==0)
#endif

void ReadBool(TiXmlElement *element, bool& b, const char *name = "value");
void ReadFloat(TiXmlElement *element, float  &f, const char *name = "value");
void ReadVector(TiXmlElement *element, glm::vec3 &v);
void ReadVector4(TiXmlElement *element, glm::vec4 &v);

bool ParseSceneFile(const std::string SceneFile, std::vector<std::string>& modelPaths, bool& isVPLScene, Camera& m_Camera, bool& isCameraInitialized,
	SunLightConfig& sunLightConfig, int& imgWidth, int &imgHeight, float &exposure, std::shared_ptr<SimpleAnimation>& animation)
{

	std::cout << "Loading scene description file \"" << SceneFile << "\"...\n";

	TiXmlDocument doc(SceneFile.c_str());
	if (!doc.LoadFile()) {
		printf("Failed to load the file \"%s\"\n", SceneFile);
		return 0;
	}

	TiXmlElement *xml = doc.FirstChildElement("scenedescription");
	if (!xml) {
		printf("No \"scenedescription\" tag found.\n");
		return 0;
	}

	const char* scenetype = xml->Attribute("type");
	if (scenetype && COMPARE(scenetype, "vpl"))
	{
		isVPLScene = true;
	}

	TiXmlElement *model = xml->FirstChildElement("model");

	if (!model) 
	{
		printf("No \"model\" tag found.\n");
		return 0;
	}
	else
	{
		TiXmlElement *cur = model;
		while (cur)
		{
			const char* path = cur->Attribute("path");
			if (path)
			{
				modelPaths.push_back(path);
			}
			else
			{
				printf("No \"path\" attribute found in \"model\" tag.\n");
				return 0;
			}

			cur = cur->NextSiblingElement();
		}
	}

	// default values
	glm::vec3 pos(0,0,0), dir(0,-1,0), up(0,1,0);
	float fov = 1, nearClip = 1.0, farClip = 1000.0;
	imgWidth = 1280, imgHeight = 720;
	exposure = 1.0;
	float movescale = 1.0;
	float panscale = 1.0;

	isCameraInitialized = false;
	TiXmlElement *cam = xml->FirstChildElement("camera");
	if (cam) 
	{
		isCameraInitialized = true;
		TiXmlElement *camChild = cam->FirstChildElement();
		while (camChild) 
		{
			if (COMPARE(camChild->Value(), "position")) ReadVector(camChild, pos);
			else if (COMPARE(camChild->Value(), "forward")) ReadVector(camChild, dir);
			else if (COMPARE(camChild->Value(), "up")) ReadVector(camChild, up);
			else if (COMPARE(camChild->Value(), "fov")) ReadFloat(camChild, fov);
			else if (COMPARE(camChild->Value(), "width")) camChild->QueryIntAttribute("value", &imgWidth);
			else if (COMPARE(camChild->Value(), "height")) camChild->QueryIntAttribute("value", &imgHeight);
			else if (COMPARE(camChild->Value(), "near")) ReadFloat(camChild, nearClip);
			else if (COMPARE(camChild->Value(), "far")) ReadFloat(camChild, farClip);
			else if (COMPARE(camChild->Value(), "exposure")) ReadFloat(camChild, exposure);
			else if (COMPARE(camChild->Value(), "movescale")) ReadFloat(camChild, movescale);
			else if (COMPARE(camChild->Value(), "panscale")) ReadFloat(camChild, panscale);
			else if (COMPARE(camChild->Value(), "animate"))
			{
				bool useAnimation = false;
				ReadBool(camChild, useAnimation);
				if (useAnimation) m_Camera.m_UseCameraAnimation = true;
			}
			else if (COMPARE(camChild->Value(), "path"))
			{

				TiXmlElement *pathChild = camChild->FirstChildElement();
				float time;
				glm::vec3 p_pos(0, 0, 0), p_dir(0, -1, 0), p_up(0, 1, 0);
				std::shared_ptr<ObjectPath> path = std::make_shared<ObjectPath>();

				while (pathChild)
				{
					TiXmlElement *keyframeChild = pathChild->FirstChildElement();
					while (keyframeChild)
					{
						if (COMPARE(keyframeChild->Value(), "time")) ReadFloat(keyframeChild, time);
						else if (COMPARE(keyframeChild->Value(), "pos")) ReadVector(keyframeChild, p_pos);
						else if (COMPARE(keyframeChild->Value(), "target")) ReadVector(keyframeChild, p_dir);
						else if (COMPARE(keyframeChild->Value(), "up")) ReadVector(keyframeChild, p_up);
						keyframeChild = keyframeChild->NextSiblingElement();
					}
					path->addKeyFrame(time, p_pos, p_dir, p_up);
					pathChild = pathChild->NextSiblingElement();
				}
				m_Camera.SetObjectPath(path);
			}
			camChild = camChild->NextSiblingElement();
		}
		m_Camera.SetEyeAtUp(Vector3(pos.x, pos.y, pos.z), Vector3(pos.x+dir.x,pos.y+dir.y,pos.z+dir.z), Vector3(up.x,up.y,up.z));
		m_Camera.SetFOV(fov * PI / 180);
		m_Camera.SetZRange(nearClip, farClip);
		m_Camera.m_AngleMultipler = panscale;
		m_Camera.m_DistanceMultipler = movescale;
	}

	TiXmlElement *light = xml->FirstChildElement("light");
	// default values
	sunLightConfig.lightIntensity = 3.0;
	sunLightConfig.inclinationAnimPhase = 0.86;
	sunLightConfig.orientationAnimPhase = 1.16;
	sunLightConfig.hasAnimation = false;

	if (light) 
	{
		const char* type = light->Attribute("type");
		if (type)
		{
			if (!COMPARE(type, "directional"))
			{
				printf("Current version only supports directional lights.\n");
				return 0;
			}
		}

		TiXmlElement *lightChild = light->FirstChildElement();
		while (lightChild) 
		{
			if (COMPARE(lightChild->Value(), "intensity")) ReadFloat(lightChild, sunLightConfig.lightIntensity);
			else if (COMPARE(lightChild->Value(), "orientation"))
			{
				ReadFloat(lightChild, sunLightConfig.orientationAnimPhase);
				double d;
				int res = lightChild->QueryDoubleAttribute("frequency", &d);
				if (res == TIXML_SUCCESS)
				{
					sunLightConfig.hasAnimation = true;
					sunLightConfig.orientationAnimFreq = (float)d;
				}
			}
			else if (COMPARE(lightChild->Value(), "inclination"))
			{
				ReadFloat(lightChild, sunLightConfig.inclinationAnimPhase);
				double d;
				int res = lightChild->QueryDoubleAttribute("frequency", &d);
				if (res == TIXML_SUCCESS)
				{
					sunLightConfig.hasAnimation = true;
					sunLightConfig.inclinationAnimFreq = (float)d;
				}
			}
			lightChild = lightChild->NextSiblingElement();
		}
	}

	TiXmlElement *anim = xml->FirstChildElement("animation");

	if (anim)
	{
		animation = std::make_shared<SimpleAnimation>("SimpleAnimation", FLT_MAX);
		TiXmlElement *mesh = anim->FirstChildElement("meshid");
		while (mesh)
		{
			int meshid;
			mesh->QueryIntAttribute("value", &meshid);

			int channelId = animation->addChannel(meshid + 1);

			TiXmlElement* xform = mesh->FirstChildElement();
			while (xform)
			{
				SimpleAnimation::AnimProcedure proc;
				if (COMPARE(xform->Value(), "rotate"))
				{
					proc.type = SimpleAnimation::AnimProcedure::ProcedureType::Rotation;
					TiXmlElement *rotateChild = xform->FirstChildElement();
					while (rotateChild)
					{
						if (COMPARE(rotateChild->Value(), "axis")) ReadVector(rotateChild, proc.rotation_axis);
						else if (COMPARE(rotateChild->Value(), "phase")) ReadFloat(rotateChild, proc.phase);
						else if (COMPARE(rotateChild->Value(), "w")) ReadFloat(rotateChild, proc.w);
						rotateChild = rotateChild->NextSiblingElement();
					}
				}
				else if (COMPARE(xform->Value(), "translate"))
				{
					proc.type = SimpleAnimation::AnimProcedure::ProcedureType::Translation;
					TiXmlElement *translateChild = xform->FirstChildElement();
					while (translateChild)
					{
						if (COMPARE(translateChild->Value(), "x")) ReadVector4(translateChild, proc.sine_x);
						else if (COMPARE(translateChild->Value(), "y")) ReadVector4(translateChild, proc.sine_y);
						else if (COMPARE(translateChild->Value(), "z")) ReadVector4(translateChild, proc.sine_z);
						translateChild = translateChild->NextSiblingElement();
					}
				}

				animation->addProcedure(channelId, proc);
				xform = xform->NextSiblingElement();
			}
			mesh = mesh->NextSiblingElement();
		}
	}



	return 1;
}

void ReadBool(TiXmlElement *element, bool &b, const char *name)
{
	element->QueryBoolAttribute(name, &b);
}

void ReadFloat(TiXmlElement *element, float &f, const char *name)
{
	double d = (double)f;
	element->QueryDoubleAttribute(name, &d);
	f = (float)d;
}

void ReadVector(TiXmlElement *element, glm::vec3 &v)
{
	double x = (double)v.x;
	double y = (double)v.y;
	double z = (double)v.z;
	element->QueryDoubleAttribute("x", &x);
	element->QueryDoubleAttribute("y", &y);
	element->QueryDoubleAttribute("z", &z);
	v.x = (float)x;
	v.y = (float)y;
	v.z = (float)z;

	float f = 1;
	ReadFloat(element, f);
	v *= f;
}

void ReadVector4(TiXmlElement *element, glm::vec4 &v)
{
	double x = (double)v.x;
	double y = (double)v.y;
	double z = (double)v.z;
	double w = (double)v.w;
	element->QueryDoubleAttribute("a", &x);
	element->QueryDoubleAttribute("b", &y);
	element->QueryDoubleAttribute("c", &z);
	element->QueryDoubleAttribute("d", &w);
	v.x = (float)x;
	v.y = (float)y;
	v.z = (float)z;
	v.w = (float)w;

	float f = 1;
	ReadFloat(element, f);
	v *= f;
}
