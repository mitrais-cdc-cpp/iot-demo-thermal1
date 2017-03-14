#pragma once

namespace mitrais {
	namespace config {
		namespace aws {
			static char* AWS_ENDPOINT = "xx";
		    static const char* AWS_KEY = "xx";
		    static const char* AWS_SECRET = "xx";
		    static const char* AWS_REGION = "xx";
		    static const char* AWS_THERMAL_TOPIC = "xx";
		    static const char* AWS_LIGHT_TOPIC = "xx";
		    static const char* AWS_SUBSCRIBED_TOPIC = "xx";		
			static const int PORT = 0;    
		}
		
		static const char* DEVICE_ID = "xx";
		static const char* WIFI_SSID = "xx";
	    static const char* WIFI_PASSWORD = "xx";
		static unsigned long PUBLISH_THERMAL_INTERVAL = 20000; //equals to 10 secs
		static unsigned long DETECT_LIGHT_INTERVAL = 5000; //equals to 5 secs
		static unsigned long SUBSCRIBE_TOPIC_INTERVAL = 5000; 
	}    
}