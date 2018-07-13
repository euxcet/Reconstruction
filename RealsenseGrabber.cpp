#include "RealsenseGrabber.h"
#include "Timer.h"
#include "librealsense2/hpp/rs_sensor.hpp"
#include "librealsense2/hpp/rs_processing.hpp"

RealsenseGrabber::RealsenseGrabber()
{
	depthFilter = new DepthFilter();
	colorFilter = new ColorFilter();
	alignColorMap = new AlignColorMap();
	depthImages = new UINT16*[MAX_CAMERAS];
	colorImages = new UINT8*[MAX_CAMERAS];
	for (int i = 0; i < MAX_CAMERAS; i++) {
		depthImages[i] = new UINT16[DEPTH_H * DEPTH_W];
		colorImages[i] = new UINT8[2 * COLOR_H * COLOR_W];
		memset(depthImages[i], 0, DEPTH_H * DEPTH_W * sizeof(UINT16));
		memset(colorImages[i], 0, 2 * COLOR_H * COLOR_W * sizeof(UINT8));
	}
	colorImagesRGB = new RGBQUAD*[MAX_CAMERAS];
	for (int i = 0; i < MAX_CAMERAS; i++) {
		colorImagesRGB[i] = new RGBQUAD[COLOR_H * COLOR_W];
	}
	depth2color = new Transformation[MAX_CAMERAS];
	color2depth = new Transformation[MAX_CAMERAS];
	depthIntrinsics = new Intrinsics[MAX_CAMERAS];
	colorIntrinsics = new Intrinsics[MAX_CAMERAS];

	rs2::context context;
	for (auto&& device : context.query_devices()) {
		enableDevice(device);
	}
}

RealsenseGrabber::~RealsenseGrabber()
{
	if (depthFilter != NULL) {
		delete depthFilter;
	}
	if (colorFilter != NULL) {
		delete colorFilter;
	}
	if (alignColorMap != NULL) {
		delete alignColorMap;
	}
	if (depthImages != NULL) {
		for (int i = 0; i < MAX_CAMERAS; i++) {
			if (depthImages[i] != NULL) {
				delete depthImages[i];
			}
		}
		delete[] depthImages;
	}
	if (colorImages != NULL) {
		for (int i = 0; i < MAX_CAMERAS; i++) {
			if (colorImages[i] != NULL) {
				delete colorImages[i];
			}
		}
		delete[] colorImages;
	}
	if (colorImagesRGB != NULL) {
		for (int i = 0; i < MAX_CAMERAS; i++) {
			if (colorImagesRGB[i] != NULL) {
				delete colorImagesRGB[i];
			}
		}
		delete[] colorImagesRGB;
	}
	if (depth2color != NULL) {
		delete[] depth2color;
	}
	if (color2depth != NULL) {
		delete[] color2depth;
	}
	if (depthIntrinsics != NULL) {
		delete[] depthIntrinsics;
	}
	if (colorIntrinsics != NULL) {
		delete[] colorIntrinsics;
	}
	for (int i = 0; i < devices.size(); i++) {
		devices[i].stop();
	}
}

void RealsenseGrabber::enableDevice(rs2::device device)
{
	std::string serialNumber(device.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER));

	if (strcmp(device.get_info(RS2_CAMERA_INFO_NAME), "Platform Camera") == 0) {
		return;
	}

	rs2::config cfg;
	cfg.enable_device(serialNumber);
	cfg.enable_stream(RS2_STREAM_DEPTH, DEPTH_W, DEPTH_H, RS2_FORMAT_Z16, 60);
	cfg.enable_stream(RS2_STREAM_COLOR, COLOR_W, COLOR_H, RS2_FORMAT_YUYV, 60);

	rs2::pipeline pipeline;
	pipeline.start(cfg);

	devices.push_back(pipeline);

	std::vector<rs2::sensor> sensors = device.query_sensors();
	for (int i = 0; i < sensors.size(); i++) {
		if (strcmp(sensors[i].get_info(RS2_CAMERA_INFO_NAME), "Stereo Module") == 0) {
			float depth_unit = sensors[i].get_option(RS2_OPTION_DEPTH_UNITS);
			float stereo_baseline = sensors[i].get_option(RS2_OPTION_STEREO_BASELINE) * 0.001;
			convertFactors.push_back(stereo_baseline * (1 << 5) / depth_unit);
			break;
		}
	}
	/* Transform convertFactors */
}

void RealsenseGrabber::convertYUVtoRGBA(UINT8* src, RGBQUAD* dst) {
	INT16 U, V;
	for (int i = 0; i < COLOR_H * COLOR_W; i++) {
		if ((i & 1) == 0) {
			U = src[i * 2 + 1];
			V = src[i * 2 + 3];
		}
		INT16 Y = src[i * 2];
		INT16 C = Y - 16;
		INT16 D = U - 128;
		INT16 E = V - 128;
		dst[i].rgbBlue = max(0, min(255, (298 * C + 409 * E + 128) >> 8));
		dst[i].rgbGreen = max(0, min(255, (298 * C - 100 * D - 208 * E + 128) >> 8));
		dst[i].rgbRed = max(0, min(255, (298 * C + 516 * D + 128) >> 8));
	}
}

int RealsenseGrabber::getRGBD(float*& depthImages_device, RGBQUAD*& colorImages_device, Transformation*& color2depth, Intrinsics*& depthIntrinsics, Intrinsics*& colorIntrinsics, Transmission* transmission)
{

#ifdef TRANSMISSION
	transmission.sendInt(devices.size());
#endif

	for (int deviceId = 0; deviceId < devices.size(); deviceId++) {
		rs2::pipeline pipeline = devices[deviceId];
		rs2::frameset frameset;
		while (!pipeline.poll_for_frames(&frameset)) {
			Sleep(1);
		}

		if (frameset.size() > 0) {
			rs2::stream_profile depthProfile;
			rs2::stream_profile colorProfile;

			for (int i = 0; i < frameset.size(); i++) {
				rs2::frame frame = frameset[i];
				rs2::stream_profile profile = frame.get_profile();


				rs2_intrinsics intrinsics = profile.as<rs2::video_stream_profile>().get_intrinsics();


				if (profile.stream_type() == RS2_STREAM_DEPTH) {
					/* send intrinsics */
					#ifdef TRANSMISSION
						transmission.sendInt(0);
						transmission.sendFloat(intrinsics.fx);
						transmission.sendFloat(intrinsics.fy);
						transmission.sendFloat(intrinsics.ppx);
						transmission.sendFloat(intrinsics.ppy);
					#endif

					depthProfile = profile;
					this->depthIntrinsics[deviceId].fx = intrinsics.fx;
					this->depthIntrinsics[deviceId].fy = intrinsics.fy;
					this->depthIntrinsics[deviceId].ppx = intrinsics.ppx;
					this->depthIntrinsics[deviceId].ppy = intrinsics.ppy;
					depthFilter->setConvertFactor(deviceId, intrinsics.fx * convertFactors[deviceId]);

					memcpy(this->depthImages[deviceId], frame.get_data(), DEPTH_H * DEPTH_W * sizeof(UINT16));
					/* send frame.get_data() */
					#ifdef TRANSMISSION
						transmission.sendArray((char*)this->depthImages[deviceId], DEPTH_H * DEPTH_W * sizeof(UINT16));
					#endif
				}
				if (profile.stream_type() == RS2_STREAM_COLOR) {
					#ifdef TRANSMISSION
						transmission.sendInt(1);
						transmission.sendFloat(intrinsics.fx);
						transmission.sendFloat(intrinsics.fy);
						transmission.sendFloat(intrinsics.ppx);
						transmission.sendFloat(intrinsics.ppy);
					#endif
					colorProfile = profile;
					this->colorIntrinsics[deviceId].fx = intrinsics.fx;
					this->colorIntrinsics[deviceId].fy = intrinsics.fy;
					this->colorIntrinsics[deviceId].ppx = intrinsics.ppx;
					this->colorIntrinsics[deviceId].ppy = intrinsics.ppy;

					memcpy(this->colorImages[deviceId], frame.get_data(), 2 * COLOR_H * COLOR_W * sizeof(UINT8));
					/* send frame.get_data() */
					#ifdef TRANSMISSION
						transmission.sendArray((char*)this->colorImages[deviceId], 2 * COLOR_H * COLOR_W * sizeof(UINT8));
					#endif
				}
			}

			rs2_extrinsics d2cExtrinsics = depthProfile.get_extrinsics_to(colorProfile);
			this->depth2color[deviceId] = Transformation(d2cExtrinsics.rotation, d2cExtrinsics.translation);

			/* send depth2color */
			#ifdef TRANSMISSION
				transmission.sendArray((char*)d2cExtrinsics.rotation, 9 * sizeof(float));
				transmission.sendArray((char*)d2cExtrinsics.translation, 3 * sizeof(float));
			#endif

			rs2_extrinsics c2dExtrinsics = colorProfile.get_extrinsics_to(depthProfile);
			this->color2depth[deviceId] = Transformation(c2dExtrinsics.rotation, c2dExtrinsics.translation);

			/* send color2depth */
			#ifdef TRANSMISSION
				transmission.sendArray((char*)c2dExtrinsics.rotation, 9 * sizeof(float));
				transmission.sendArray((char*)c2dExtrinsics.translation, 3 * sizeof(float));
			#endif
		}
	}

	/*
		Receive remote data
	*/

#ifdef TRNASMISSION
	int remoteDevices;
	transmission.receiveInt(remoteDevices);
	for(int deviceId = devices.size(); deviceId < devices.size() + remoteDevices; deviceId++) {
		int type = transmission.receiveInt();
		if (type == 0) { // DEPTH
			transmission.receiveFloat(this.depthIntrinsics[deviceId].fx);
			transmission.receiveFloat(this.depthIntrinsics[deviceId].fy);
			transmission.receiveFloat(this.depthIntrinsics[deviceId].ppx);
			transmission.receiveFloat(this.depthIntrinsics[deviceId].ppy);

			/*
			???
			depthFilter->setConvertFactor(deviceId, intrinsics.fx * convertFactors[deviceId]);
			*/

			transmission.receiveArray((char*)this->depthImages[deviceId], DEPTH_H * DEPTH_W * sizeof(UINT16));
		}
		else if (type == 1) { // COLOR
			transmission.receiveFloat(this.colorIntrinsics[deviceId].fx);
			transmission.receiveFloat(this.colorIntrinsics[deviceId].fy);
			transmission.receiveFloat(this.colorIntrinsics[deviceId].ppx);
			transmission.receiveFloat(this.colorIntrinsics[deviceId].ppy);

			transmission.receiveArray((char*)this->colorImages[deviceId], 2 * COLOR_H * COLOR_W * sizeof(UINT8));
		}

		float *rotation = new float[9];
		float *translation = new float[3];
		transmission.receiveArray((char*)rotation, 9 * sizeof(float));
		transmission.receiveArray((char*)translation, 3 * sizeof(float));
		this->depth2color[deviceId] = Transformation(rotation, translation);

		transmission.receiveArray((char*)rotation, 9 * sizeof(float));
		transmission.receiveArray((char*)translation, 3 * sizeof(float));
		this->color2depth[deviceId] = Transformation(rotation, translation);
		delete[] rotation;
		delete[] translation;
	}
#endif




	for (int i = 0; i < devices.size(); i++) {
		if (this->depthImages[i] != NULL) {
			depthFilter->process(i, this->depthImages[i]);
		}
	}

	for (int i = 0; i < devices.size(); i++) {
		if (this->colorImages[i] != NULL) {
			colorFilter->process(i, this->colorImages[i]);
		}
	}

	depthImages_device = depthFilter->getCurrFrame_device();
	colorImages_device = colorFilter->getCurrFrame_device();
	color2depth = this->color2depth;
	depthIntrinsics = this->depthIntrinsics;
	colorIntrinsics = this->colorIntrinsics;

	RGBQUAD* alignedColorMap = alignColorMap->getAlignedColor_device(devices.size(), depthImages_device, colorImages_device, depthIntrinsics, colorIntrinsics, depth2color);
	colorImages_device = alignedColorMap;
	for (int i = 0; i < devices.size(); i++) {
		colorIntrinsics[i] = depthIntrinsics[i].zoom((float)COLOR_W / DEPTH_W, (float)COLOR_H / DEPTH_H);
	}

	return devices.size();
}

int RealsenseGrabber::getRGB(RGBQUAD**& colorImages, Intrinsics*& colorIntrinsics)
{
	for (int deviceId = 0; deviceId < devices.size(); deviceId++) {
		rs2::pipeline pipeline = devices[deviceId];
		rs2::frameset frameset;
		while (!pipeline.poll_for_frames(&frameset)) {
			Sleep(1);
		}

		if (frameset.size() > 0) {
			for (int i = 0; i < frameset.size(); i++) {
				rs2::frame frame = frameset[i];
				rs2::stream_profile profile = frame.get_profile();
				rs2_intrinsics intrinsics = profile.as<rs2::video_stream_profile>().get_intrinsics();

				if (profile.stream_type() == RS2_STREAM_COLOR) {
					this->colorIntrinsics[deviceId].fx = intrinsics.fx;
					this->colorIntrinsics[deviceId].fy = intrinsics.fy;
					this->colorIntrinsics[deviceId].ppx = intrinsics.ppx;
					this->colorIntrinsics[deviceId].ppy = intrinsics.ppy;
					memcpy(this->colorImages[deviceId], frame.get_data(), 2 * COLOR_W * COLOR_H * sizeof(UINT8));
				}
			}
		}
	}

	for (int i = 0; i < devices.size(); i++) {
		if (this->colorImages[i] != NULL) {
			convertYUVtoRGBA(this->colorImages[i], this->colorImagesRGB[i]);
		}
		else {
			std::cout << "Disconnected!" << std::endl;
		}
	}

	colorImages = this->colorImagesRGB;
	colorIntrinsics = this->colorIntrinsics;
	return devices.size();
}
