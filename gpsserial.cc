/*
    This file is part of Kismet

    Kismet is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Kismet is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Kismet; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "config.h"
#include "gpsserial.h"
#include "configfile.h"
#include "speechcontrol.h"
#include "soundcontrol.h"
#include "packetchain.h"

GPSSerial::GPSSerial() {
    fprintf(stderr, "FATAL OOPS: gpsserial called with no globalreg\n");
	exit(-1);
}

GPSSerial::GPSSerial(GlobalRegistry *in_globalreg) : GPSCore(in_globalreg) {
	// Make a serial port object
	sercli = new SerialClient(globalreg);
	netclient = sercli;

    // Attach it to ourselves and opposite
    RegisterNetworkClient(sercli);
    sercli->RegisterClientFramework(this);

	if (globalreg->kismet_config->FetchOpt("gpsdevice") == "") {
		_MSG("Missing 'gpsdevice' option in config, but gpstype set to serial",
			 MSGFLAG_FATAL);
		globalreg->fatal_condition = 1;
		return;
	}

	ScanOptions();
	RegisterComponents();

	snprintf(device, 128, "%s", 
			 globalreg->kismet_config->FetchOpt("gpsdevice").c_str());

	if (sercli->Connect(device, 0) < 0) {
		_MSG("GPSSerial: Could not open serial port " + string(device),
			 MSGFLAG_ERROR);
		if (reconnect_attempt < 0) {
			_MSG("GPSSerial: Reconnection not enabled (gpsreconnect), disabling "
				 "GPS", MSGFLAG_ERROR);
			return;
		}
		last_disconnect = time(0);
	} else {
		// Configure the device - 9600,N,8,1 hardcoded for now
		struct termios options;

		sercli->GetOptions(&options);

		// 9600
		cfsetispeed(&options, B9600);
		cfsetospeed(&options, B9600);

		options.c_cflag |= (CLOCAL | CREAD);

		// Set 8-n-1
		options.c_cflag &= ~PARENB;
		options.c_cflag &= ~CSTOPB;
		options.c_cflag &= ~CSIZE;
		options.c_cflag |= CS8;

		sercli->SetOptions(&options);
	}

	_MSG("Using GPS device on " + string(device), MSGFLAG_INFO);
}

GPSSerial::~GPSSerial() {
	// Unregister ourselves from the main tcp service loop
	globalreg->RemovePollableSubsys(this);
	
    if (sercli != NULL && sercli->Valid()) {
        sercli->KillConnection();
        delete sercli;
    }
}

int GPSSerial::KillConnection() {
    if (sercli != NULL && sercli->Valid())
        sercli->KillConnection();

    return 1;
}

int GPSSerial::Shutdown() {
    if (sercli != NULL) {
        sercli->FlushRings();
        sercli->KillConnection();
    }

    return 1;
}

int GPSSerial::Reconnect() {
    if (sercli->Connect(device, 0) < 0) {
        snprintf(errstr, STATUS_MAX, "GPSSerial: Could not open GPS device %s, will "
                 "reconnect in %d seconds", device, 
				 kismin(reconnect_attempt + 1, 6) * 5);
        globalreg->messagebus->InjectMessage(errstr, MSGFLAG_ERROR);
        reconnect_attempt++;
        last_disconnect = time(0);
        return 0;
    }

	// Configure the device - 9600,N,8,1 hardcoded for now
	struct termios options;

	sercli->GetOptions(&options);

	// 9600
	cfsetispeed(&options, B9600);
	cfsetospeed(&options, B9600);

	options.c_cflag |= (CLOCAL | CREAD);

	// Set 8-n-1
	options.c_cflag &= ~PARENB;
	options.c_cflag &= ~CSTOPB;
	options.c_cflag &= ~CSIZE;
	options.c_cflag |= CS8;

	sercli->SetOptions(&options);

    return 1;
}

int GPSSerial::ParseData() {
	int len, rlen;
	char *buf;

	double in_lat = 0, in_lon = 0, in_spd = 0, in_alt = 0, in_hed = 0;
	int in_mode = 0, set_data, set_spd;

	if (netclient == NULL)
		return 0;

	if (netclient->Valid() == 0)
		return 0;

	len = netclient->FetchReadLen();
	buf = new char[len + 1];

	if (netclient->ReadData(buf, len, &rlen) < 0) {
		_MSG("GPSSerial parser failed to get data from the serial port",
			 MSGFLAG_ERROR);
		return -1;
	}

	buf[len] = '\0';

	vector<string> inptok = StrTokenize(buf, "\n", 0);
	delete[] buf;

	if (inptok.size() < 1) {
		return 0;
	}

	set_data = 0;
	set_spd = 0;

	for (unsigned int it = 0; it < inptok.size(); it++) {
		if (netclient->Valid()) {
			netclient->MarkRead(inptok[it].length() + 1);
		}

		if (inptok[it].length() < 4)
			continue;

		// $GPGGA,012527.000,4142.6918,N,07355.8711,W,1,07,1.2,57.8,M,-34.0,M,,0000*57

		vector<string> gpstoks = StrTokenize(inptok[it], ",");

		if (gpstoks.size() == 0)
			continue;

		if (gpstoks[0] == "$GPGGA") {
			int tint;
			float tfloat;

			if (gpstoks.size() != 15)
				continue;

			// Parse the basic gps coodinate string
			// $GPGGA,time,lat,NS,lon,EW,quality,#sats,hdop,alt,M,geopos,M,
			// dgps1,dgps2,checksum

			if (sscanf(gpstoks[2].c_str(), "%2d%f", &tint, &tfloat) != 2)
				continue;
			in_lat = (float) tint + (tfloat / 60);
			if (gpstoks[3] == "S")
				in_lat = in_lat * -1;

			if (sscanf(gpstoks[4].c_str(), "%3d%f", &tint, &tfloat) != 2)
				continue;
			in_lon = (float) tint + (tfloat / 60);
			if (gpstoks[5] == "W")
				in_lon = in_lon * -1;

			if (sscanf(gpstoks[6].c_str(), "%d", &tint) != 1)
				continue;
			in_mode = tint;

			// Kluge the # of sats into the mode - if we see more than 3,
			// we've got a good chance of a 3d lock.  Emulate gpsd modes
			if (sscanf(gpstoks[7].c_str(), "%d", &tint) != 1)
				continue;
			if (tint > 3 && in_mode == 1)
				in_mode = 3;
			else
				in_mode = 2;

			if (sscanf(gpstoks[9].c_str(), "%f", &tfloat) != 1)
				continue;
			in_alt = tfloat;

			// printf("debug - %f, %f mode %d alt %f\n", in_lat, in_lon, in_mode, in_alt);
			set_data = 1;

			continue;
		}

		// Travel made good
		if (gpstoks[0] == "$GPVTG") {
			// $GPVTG,,T,,M,0.00,N,0.0,K,A*13
			float tfloat;

			if (gpstoks.size() != 10) {
				continue;
			}

			if (sscanf(gpstoks[7].c_str(), "%f", &tfloat) != 1) 
				continue;
			in_spd = tfloat;

			// printf("debug - spd %f\n", in_spd);

			set_spd = 1;

			continue;
		}

	}

	if (set_data) {
		last_lat = lat;
		lat = in_lat;
		last_lon = lon;
		lon = in_lon;

		alt = in_alt;

		last_hed = hed;
		hed = CalcHeading(lat, lon, last_lat, last_lon);

        if (mode < 2 && (gps_options & GPSD_OPT_FORCEMODE)) {
            mode = 2;
        } else {
            if (mode < 2 && in_mode >= 2) {
                    globalreg->speechctl->SayText("Got G P S position fix");
                    globalreg->soundctl->PlaySound("gpslock");
            } else if (mode >= 2 && in_mode < 2) {
                    globalreg->speechctl->SayText("Lost G P S position fix");
                    globalreg->soundctl->PlaySound("gpslost");
            }

            mode = in_mode;
        }
	}

	if (set_spd)
		spd = in_spd;

	// printf("debug - got data and speed, %f,%f fix %d alt %f spd %f\n", lat, lon, mode, alt, spd);

#if 0
    // send it to the client
    gps_data gdata;

    snprintf(errstr, 32, "%lf", lat);
    gdata.lat = errstr;
    snprintf(errstr, 32, "%lf", lon);
    gdata.lon = errstr;
    snprintf(errstr, 32, "%lf", alt);
    gdata.alt = errstr;
    snprintf(errstr, 32, "%lf", spd);
    gdata.spd = errstr;
    snprintf(errstr, 32, "%lf", hed);
    gdata.heading = errstr;
    snprintf(errstr, 32, "%d", mode);
    gdata.mode = errstr;

    globalreg->kisnetserver->SendToAll(gps_proto_ref, (void *) &gdata);

	// Make an empty packet w/ just GPS data for the gpsxml logger to catch
	kis_packet *newpack = globalreg->packetchain->GeneratePacket();
	kis_gps_packinfo *gpsdat = new kis_gps_packinfo;
	newpack->ts.tv_sec = globalreg->timestamp.tv_sec;
	newpack->ts.tv_usec = globalreg->timestamp.tv_usec;
	FetchLoc(&(gpsdat->lat), &(gpsdat->lon), &(gpsdat->alt),
			 &(gpsdat->spd), &(gpsdat->heading), &(gpsdat->gps_fix));
	newpack->insert(_PCM(PACK_COMP_GPS), gpsdat);
	globalreg->packetchain->ProcessPacket(newpack);
#endif

    return 1;
}

int GPSSerial::Timer() {
    // Timed backoff up to 30 seconds
    if (netclient->Valid() == 0 && reconnect_attempt >= 0 &&
        (time(0) - last_disconnect >= (kismin(reconnect_attempt, 6) * 5))) {
        if (Reconnect() <= 0)
            return 0;
    }

	return GPSCore::Timer();
}
