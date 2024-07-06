/*
 * Copyright (C) 2020  Christian Berger
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// Include the single-file, header-only middleware libcluon to create high-performance microservices
#include "cluon-complete.hpp"
// Include the OpenDLV Standard Message Set that contains messages that are usually exchanged for automotive or robotic applications 
#include "opendlv-standard-message-set.hpp"

// Include the GUI and image processing header files from OpenCV
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

// GROUND STEERING CALCULATIONS
auto steeringAngle(double blue, double yellow, double pedalPos, std::string carDirection, double velocityZ){
    //finding the difference between the blue and yellow cone angles
    	double difference = 0.0;
        //the difference between the yellow and blue angles is calculated differently depending on carDirection (found from data analysis)...
    	if (carDirection == "Clockwise"){ 
    		if(velocityZ<=0){ //if velocity is negative subtract the blue angle from the yellow angle
        	difference = blue - yellow; //
        	}else{ //if positive subtract the yellow angle from the blue angle
        	difference =yellow - blue ;
        	}
    	}else if (carDirection == "Counter-Clockwise"){
    		if(velocityZ<=0){ //if the velocity is negative subtract the yellow angle from the blue angle
        	difference = yellow - blue;
        	}else{  //if positive subtract the blue angle from the yellow angle
        	difference = blue - yellow;
        	}
    	}
        
    //getting the groundSteering value
   	double groundS = 0.0;
	if (pedalPos == 0){ //if pedal position is 0 then groundSteering is also 0 (found from data analysis)
		groundS = 0.0;
	} else {
		if(velocityZ < 8 && velocityZ >= -8){ //if angularZ greater than or equal to -8  and less than 8 then groundSteering is -0.02 (found from data analysis)
		groundS = -0.02;
		}else if(velocityZ < -55 ){ //if angularZ is less than -55 then groundSteering is -0.3 (found from data analysis)
		groundS=-0.3;
		}else{ //otherwise, take the difference calculated earlier and calculate the groundSteering with this equation 0.0641*x+0.0387 (found from data analysis)
		groundS = 0.0641 * difference + 0.0387;
		}
	}
    //return groundSteering variable
    return groundS;
}

int32_t main(int32_t argc, char **argv) {
    int32_t retCode{1};
    // Parse the command line parameters as we require the user to specify some mandatory information on startup.
    auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
    if ( (0 == commandlineArguments.count("cid")) ||
         (0 == commandlineArguments.count("name")) ||
         (0 == commandlineArguments.count("width")) ||
         (0 == commandlineArguments.count("height")) ) {
        std::cerr << argv[0] << " attaches to a shared memory area containing an ARGB image." << std::endl;
        std::cerr << "Usage:   " << argv[0] << " --cid=<OD4 session> --name=<name of shared memory area> [--verbose]" << std::endl;
        std::cerr << "         --cid:    CID of the OD4Session to send and receive messages" << std::endl;
        std::cerr << "         --name:   name of the shared memory area to attach" << std::endl;
        std::cerr << "         --width:  width of the frame" << std::endl;
        std::cerr << "         --height: height of the frame" << std::endl;
        std::cerr << "Example: " << argv[0] << " --cid=253 --name=img --width=640 --height=480 --verbose" << std::endl;
    }
    else {
        // Extract the values from the command line parameters
        const std::string NAME{commandlineArguments["name"]};
        const uint32_t WIDTH{static_cast<uint32_t>(std::stoi(commandlineArguments["width"]))};
        const uint32_t HEIGHT{static_cast<uint32_t>(std::stoi(commandlineArguments["height"]))};
        const bool VERBOSE{commandlineArguments.count("verbose") != 0};

        // Attach to the shared memory.
        std::unique_ptr<cluon::SharedMemory> sharedMemory{new cluon::SharedMemory{NAME}};
        if (sharedMemory && sharedMemory->valid()) {
            std::clog << argv[0] << ": Attached to shared memory '" << sharedMemory->name() << " (" << sharedMemory->size() << " bytes)." << std::endl;

            // Interface to a running OpenDaVINCI session where network messages are exchanged.
            // The instance od4 allows you to send and receive messages.
            cluon::OD4Session od4{static_cast<uint16_t>(std::stoi(commandlineArguments["cid"]))};

            opendlv::proxy::GroundSteeringRequest gsr;
            std::mutex gsrMutex;
            auto onGroundSteeringRequest = [&gsr, &gsrMutex](cluon::data::Envelope &&env){
                // The envelope data structure provide further details, such as sampleTimePoint as shown in this test case:
                // https://github.com/chrberger/libcluon/blob/master/libcluon/testsuites/TestEnvelopeConverter.cpp#L31-L40
                std::lock_guard<std::mutex> lck(gsrMutex);
                gsr = cluon::extractMessage<opendlv::proxy::GroundSteeringRequest>(std::move(env));
                //std::cout << "lambda: groundSteering = " << gsr.groundSteering() << std::endl;
            };

            od4.dataTrigger(opendlv::proxy::GroundSteeringRequest::ID(), onGroundSteeringRequest);
            
         // Pedal Position
            opendlv::proxy::PedalPositionRequest pedalPosition;
            std::mutex pedalPositionMutex;

             auto onPedalPosition = [&pedalPosition, &pedalPositionMutex](cluon::data::Envelope &&env)
            {
                std::lock_guard<std::mutex> lck(pedalPositionMutex);
                pedalPosition = cluon::extractMessage<opendlv::proxy::PedalPositionRequest>(std::move(env));
              
             };
            
            od4.dataTrigger(opendlv::proxy::PedalPositionRequest::ID(), onPedalPosition);
            
     	// Angular Velocity
            opendlv::proxy::AngularVelocityReading angularVelocity;
	        std::mutex angularVelocityMutex;

	        auto onAngularVelocityRequest = [&angularVelocity, &angularVelocityMutex](cluon::data::Envelope &&env){
                std::lock_guard<std::mutex> lck(angularVelocityMutex);
                angularVelocity = cluon::extractMessage<opendlv::proxy::AngularVelocityReading>(std::move(env));
	        };

	        od4.dataTrigger(opendlv::proxy::AngularVelocityReading::ID(), onAngularVelocityRequest);
/*	    
	  // Geolocation
            opendlv::proxy::GeodeticHeadingReading geolocation;
	        std::mutex geolocationMutex;

	        auto onGeodeticHeadingReading = [&geolocation, &geolocationMutex](cluon::data::Envelope &&env){
                std::lock_guard<std::mutex> lck(geolocationMutex);
                geolocation = cluon::extractMessage<opendlv::proxy::GeodeticHeadingReading>(std::move(env));
	        };

	        od4.dataTrigger(opendlv::proxy::GeodeticHeadingReading::ID(), onGeodeticHeadingReading);
        //IR (VOLTAGE)
            double leftIR;
    	    double rightIR;
            opendlv::proxy::VoltageReading infrared;
            std::mutex infraredMutex;

            auto onVoltageReading = [&infrared, &infraredMutex, &rightIR, &leftIR](cluon::data::Envelope &&env)
            {
                std::lock_guard<std::mutex> lck(infraredMutex);
                infrared = cluon::extractMessage<opendlv::proxy::VoltageReading>(std::move(env));
                if (env.senderStamp() == 3)
                {
                    rightIR = infrared.voltage();
                }
                else if (env.senderStamp() == 1)
                {
                    leftIR = infrared.voltage();
                }
            };

            od4.dataTrigger(opendlv::proxy::VoltageReading::ID(), onVoltageReading);
*/
        
            // Endless loop; end the program by pressing Ctrl-C.
            while (od4.isRunning()) {
                // OpenCV data structure to hold an image.
                cv::Mat img;

                // Wait for a notification of a new frame.
                sharedMemory->wait();

                // Lock the shared memory.
                sharedMemory->lock();
                {
                    // Copy the pixels from the shared memory into our own data structure.
                    cv::Mat wrapped(HEIGHT, WIDTH, CV_8UC4, sharedMemory->data());
                    img = wrapped.clone();
                }
//TIME STAMP
        	auto timeStampPair = sharedMemory->getTimeStamp();
                int64_t timeStampInMs = cluon::time::toMicroseconds(timeStampPair.second);
                std::string timeStamp = std::to_string(timeStampInMs);
        
	        cluon::data::TimeStamp now = cluon::time::now();
	        std::time_t now_t = now.seconds();
	        std::tm* tm_gmt = std::gmtime(&now_t); 
	        std::string month = "";
	        std::string hour = "";
	        std::string minute = "";
	        std::string sec = "";
	        if(tm_gmt->tm_mon+1<10){
	        month = "0" + std::to_string(tm_gmt->tm_mon+1);
	        } else {
	        month = std::to_string(tm_gmt->tm_mon+1);
	        }
	        if(tm_gmt->tm_hour<10){ 
	                hour = "0" + std::to_string(tm_gmt->tm_hour);
	                } else { 
	                hour = std::to_string(tm_gmt->tm_hour);
	                }
	        if(tm_gmt->tm_min<10){ 
	                minute = "0" + std::to_string(tm_gmt->tm_min);
	                } else { 
	                minute = std::to_string(tm_gmt->tm_min);
	                }
	        if(tm_gmt->tm_sec<10){ 
	                sec = "0" + std::to_string(tm_gmt->tm_sec);
	                } else { 
	                sec = std::to_string(tm_gmt->tm_sec);
	                }
	     // Printing out timestamp, ground steering and group5
	        std::string utc_time = std::to_string(tm_gmt->tm_year + 1900) + "-" + month + "-" + std::to_string(tm_gmt->tm_mday) + "T" + hour + ":" + minute + ":" + sec + "Z";
	        std::string concatenatedText = "Now: " + utc_time + "; ts: " + timeStamp + "; Group5;";
	        
                sharedMemory->unlock();
                cv::putText(img, //target image
                concatenatedText, //text
                cv::Point(10,35), //top-left position
                cv::FONT_HERSHEY_SIMPLEX,
                0.5,
                cv::Scalar(255,255,255), //font color
                2);

//IMAGE PROCESSING

        cv::Mat hsv; //HSV image
        cv::cvtColor(img, hsv, cv::COLOR_BGR2HSV);
             
        // Define the lower and upper threshold values for blue cones
        cv::Scalar lower_blue{100, 120, 40}; // Hue, Saturation, Value
        cv::Scalar upper_blue{140, 255, 255};

        // Define the lower and upper threshold values for yellow cones
        cv::Scalar lower_yellow{20, 100, 100}; // Hue, Saturation, Value
        cv::Scalar upper_yellow{30, 255, 255};

        // Threshold the image to extract the blue cones
        cv::Mat blue_mask, yellow_mask;
        cv::inRange(hsv, lower_blue, upper_blue, blue_mask);
        cv::inRange(hsv, lower_yellow, upper_yellow, yellow_mask);

        // Apply a morphological opening to remove noise
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
        cv::morphologyEx(blue_mask, blue_mask, cv::MORPH_OPEN, kernel);
        cv::morphologyEx(yellow_mask, yellow_mask, cv::MORPH_OPEN, kernel);

//CONE DETECTION
        // Find contours in the thresholded image
        std::vector<std::vector<cv::Point>> blue_contours, yellow_contours;
        cv::findContours(blue_mask, blue_contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        cv::findContours(yellow_mask, yellow_contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            
            double steering;
            double blue_angle;
            double yellow_angle;
            
            // Draw bounding boxes around the detected blue cones
            for (size_t i = 0; i < blue_contours.size(); i++) {
                cv::Rect boundingRectBlue = cv::boundingRect(blue_contours[i]); //binding the detected cones
                
                if (boundingRectBlue.area() > 80 ) {
                    cv::rectangle(img, boundingRectBlue, cv::Scalar(0, 255, 0), 2); //drawing a rectangle around the cones
                }
                
                //finding the angle of the cones
                double blue_midpoint_x = boundingRectBlue.x + boundingRectBlue.width /2;
                double blue_midpoint_y = boundingRectBlue.y + boundingRectBlue.height /2;
                 blue_angle = atan(blue_midpoint_y/blue_midpoint_x);
                        
                // Draw bounding boxes around the detected yellow cones
                for (size_t j = 0; j < yellow_contours.size(); j++) {
                    cv::Rect boundingRectYellow = cv::boundingRect(yellow_contours[j]); //binding the detected cones
                    if (boundingRectYellow.area() > 80) { 
                        cv::rectangle(img, boundingRectYellow, cv::Scalar(0, 255, 0), 2); //drawing a rectangle around the cones
                    }
                    //finding the angle of the cones
                    int yellow_midpoint_x = boundingRectYellow.x + boundingRectYellow.width /2;
                    int yellow_midpoint_y = boundingRectYellow.y + boundingRectYellow.height /2;
                    yellow_angle = atan(yellow_midpoint_y/yellow_midpoint_x);

                    //variables for car direction (clockwise + counterclockwise)
                    int clockwise = 0; //count variable (clockwise)
                    int counterClockwise = 0; //count variable (counterclockwise)
                    std::string direction; //variable to save car direction

                    //if blue_angle is greater than yellow_angle then the video is clockwise (found from data analysis)
                    if (yellow_angle < blue_angle){ 
                        clockwise++;
                    }else if (yellow_angle > blue_angle){  //if yellow_angle is greater than blue_angle then the video is clockwise (found from data analysis)
                        counterClockwise++;
                    }
                    //if clockwise is greater than counterclockwise then the car is clockwise direction
                    if (clockwise > counterClockwise){
                        direction = "Clockwise";
                    }//if counterclockwise is greater than clockwise then the car is counterclockwise direction
                    else if (counterClockwise > clockwise){
                        direction = "Counter-Clockwise";
                    }
                    //variables to use as parameters for steeringAngle()   
                    double pedalPos = pedalPosition.position();
                    double velocityZ = angularVelocity.angularVelocityZ();
                    //calculating the ground steering angle using steeringAngle()
                    steering = steeringAngle(blue_angle, yellow_angle, pedalPos, direction,velocityZ);

                    //printing the output of our algorithm in the terminal
                    std::cout << "Group_05;" << timeStamp << ";" << steering << std::endl;
                }
            }
            
                // If you want to access the latest received ground steering, don't forget to lock the mutex:
                {
                    std::lock_guard<std::mutex> lck(gsrMutex);
                }

                // Display image on your screen.
                if (VERBOSE) {
                    cv::imshow(sharedMemory->name().c_str(), img);
                    cv::waitKey(1);
                }
            }
        }
        retCode = 0;
    }
    return retCode;
}
