#include <boost/bind.hpp>
#include <gazebo/gazebo.hh>
#include <gazebo/physics/physics.hh>
#include <gazebo/sensors/sensors.hh>
#include <gazebo/common/common.hh>
#include <gazebo/physics/Model.hh>
#include <gazebo/physics/Link.hh>
#include <gazebo/physics/World.hh>
#include <gazebo/sensors/RaySensor.hh>
#include <gazebo/sensors/CameraSensor.hh>
#include <gazebo/sensors/SensorManager.hh>
#include <stdio.h>
#include <gazebo/sensors/sensors.hh>
#include <vector>
#include <math.h>
#include "../utils/UdpSocket.hpp"

Socket::UdpSocket client_port(10000);
Socket::UdpSocket lrf_port(20000);
Socket::UdpSocket gps_port(20002);

int testing = 1; 

int lrf_en = 0;
int gps_en = 0;
int imu_en = 0;

struct sockaddr_in robot_addr;    

namespace gazebo
{


class AscentPlugin : public ModelPlugin
{

public: void Load(physics::ModelPtr _parent, sdf::ElementPtr /*_sdf*/)
{
    // Store the pointer to the model
    this->model = _parent;

	// Initialize sensors
    if ((this->lrf = std::dynamic_pointer_cast<gazebo::sensors::RaySensor>(
         gazebo::sensors::SensorManager::Instance()->GetSensor("laser"))) == NULL)
    {
        std::cout << "COULD NOT FIND LASER SENSOR" << std::endl;
        return;
    }

	if ((this->gps = std::dynamic_pointer_cast<gazebo::sensors::GpsSensor>(
		 gazebo::sensors::SensorManager::Instance()->GetSensor("gps"))) == NULL)
	{
		std::cout << "COULD NOT FIND GPS SENSOR" << std::endl;
		return;
	}

	if ((this->imu = std::dynamic_pointer_cast<gazebo::sensors::ImuSensor>(
		 gazebo::sensors::SensorManager::Instance()->GetSensor("imu"))) == NULL)
	{
		std::cout << "COULD NOT FIND IMU SENSOR" << std::endl;
		return;
	}
		
	// Open Udp ports 
	if(testing == 0)
	{
    	if(!client_port.Open())
    	{
        	std::cout << "Failed to open client_port!" << std::endl;
    	}
	}
	
	if(lrf_en == 1)
	{
		if(!lrf_port.Open())
    	{
        	std::cout << "Failed to open lrf_port!" << std::endl;
   		 }
	}

	// setup outgoing udp address
    memset(&robot_addr, 0, sizeof(struct sockaddr_in));    
    inet_pton(AF_INET, "192.168.1.21", &(robot_addr.sin_addr));    
    robot_addr.sin_family = AF_INET;    
    robot_addr.sin_port = htons(20001); 		
		
    // Listen to the update event. This event is broadcast every
    // simulation iteration.
    this->updateConnection = event::Events::ConnectWorldUpdateBegin(
			boost::bind(&AscentPlugin::OnUpdate, this, _1));
}

// Called by the world update start event
public: void OnUpdate(const common::UpdateInfo & /*_info*/)
{
    uint8_t buffer[256];
	memset(buffer, 0, 256);
    int rd = 0;

    if(!testing)
    {
		// drive command
        rd = client_port.Read(&buffer[0], 256, nullptr);
		printf("rd = %d\n", rd);
        //if (rd != 12)
	    //	return;
    }
    else
    {
		// fake drive command
        for(int k = 0; k < 12; k++)
        {
        	buffer[k] = 0xA;
        }
    }

	// move robot
    Drive(buffer);

	if(lrf_en == 1)
	{
   		std::vector<double> lrfData;
   		this->lrf->Ranges(lrfData);

	   	if(lrfData.size() == 0)
    	   	return;

		int len = lrfData.size()*2;

		uint8_t payload[len];


		int m = 0;
		for(int l = 0; l < lrfData.size(); l++)
		{
			lrfData.at(l) = lrfData.at(l) * 1000;
			float f = (float)lrfData.at(l);
			uint32_t uint = (uint32_t)(*(uint32_t*)&f);
			uint16_t tmp = (uint16_t)lrfData.at(l);
			payload[m++] = (uint8_t)(tmp >> 8);
			payload[m++] = (uint8_t)tmp;
		}

		if(!lrf_port.Write(&payload[0], len, (struct sockaddr*)&robot_addr))
		{
			fprintf(stderr, "Failed to send lrf to listener!\n");
			return;
		}
	}

	if(gps_en == 1)
	{
		double latitude_dec = this->gps->Latitude().Degree();
		double longitude_dec = this->gps->Longitude().Degree();

		uint8_t latitude_deg = (uint8_t)floor(latitude_dec);
		uint8_t latitude_min = (uint8_t)((latitude_dec - floor(latitude_dec)) * 60.0);
		uint8_t latitude_sec = (uint8_t)((latitude_min - floor(latitude_min)) * 60.0);
 
		uint8_t longitude_deg = (uint8_t)(floor(longitude_dec));
		uint8_t longitude_min = (uint8_t)((longitude_dec - floor(longitude_dec)) * 60.0);
		uint8_t longitude_sec = (uint8_t)((longitude_min - floor(longitude_min)) * 60.0);
  
		uint8_t gpspayload[6] = { latitude_deg, latitude_min, latitude_deg,
		                       	  longitude_deg, longitude_deg, longitude_deg };

		if(!gps_port.Write(&gpspayload[0], sizeof(gpspayload), nullptr))
		{
			fprintf(stderr, "Failed to send gps to listener!\n");
			return;
		}
	}

	if(imu_en == 1)
	{
		math::Quaternion orientation = this->imu->Orientation();

		double yaw = orientation.GetYaw();
		double yaw_degree = yaw * 180 / M_PI;  

		uint8_t tmp = (uint8_t)yaw_degree;
	}
}

private: void Drive(uint8_t* buffer)
{
	// wheels
    int lfWheels = (int)((int8_t)buffer[0]);
    int rtWheels = (-1*(int)((int8_t)buffer[1]));
	//printf("lfWlVel = %d | rtWlVel = %d\n",lfWlVel,rtWlVel);

	// arm
    int turntable = 0;
    int shoulder = 0;
    int elbow = 0;
    int wristPitch = 0;
    int wristRot = 0;
    int jaw = 0;

    this->model->GetJoint("Wheel0")->SetVelocity(0,lfWheels);	//positive = forward
    this->model->GetJoint("Wheel1")->SetVelocity(0,lfWheels);	
    this->model->GetJoint("Wheel2")->SetVelocity(0,lfWheels);	
    this->model->GetJoint("Wheel3")->SetVelocity(0,rtWheels);	//negative = forward
    this->model->GetJoint("Wheel4")->SetVelocity(0,rtWheels);	
    this->model->GetJoint("Wheel5")->SetVelocity(0,rtWheels);	

    this->model->GetJoint("Turntable")->SetVelocity(0,turntable);  	//negative = clockwise
    this->model->GetJoint("Shoulder")->SetVelocity(0,shoulder); 	//positive = up
    this->model->GetJoint("Elbow")->SetVelocity(0,elbow);  			//positive = up
    this->model->GetJoint("WristPitch")->SetVelocity(0,wristPitch); //positive = up
	this->model->GetJoint("WristRot")->SetVelocity(0,wristRot);     //negative = clockwise
    this->model->GetJoint("Jaw0")->SetVelocity(0,jaw);       		//positive = closed
    this->model->GetJoint("Jaw1")->SetVelocity(0,jaw);       		//positive = closed
}

// Pointer to the model
private: physics::ModelPtr model;

//pointer to the laserSensor
private: sensors::RaySensorPtr lrf;

//pointer to the gps sensor
private: sensors::GpsSensorPtr gps;

//pointer to the imu sensor
private: sensors::ImuSensorPtr imu;

// Pointer to the update event connection
private: event::ConnectionPtr updateConnection;
};


// Register this plugin with the simulator
GZ_REGISTER_MODEL_PLUGIN(AscentPlugin)
} 