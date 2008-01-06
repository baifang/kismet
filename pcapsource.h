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

// pcapsource is probably the most complex source handing the largest number of
// card types.  Ideally, everything should be part of the pcap source except
// wsp100 and drones.

#ifndef __PCAPSOURCE_H__
#define __PCAPSOURCE_H__

#include "config.h"

#ifdef HAVE_LIBPCAP

#include "packet.h"
#include "packetsource.h"
#include "ifcontrol.h"
#include "iwcontrol.h"

extern "C" {
#ifndef HAVE_PCAPPCAP_H
#include <pcap.h>
//#include <net/bpf.h>
#else
#include <pcap/pcap.h>
#include <pcap/net/bpf.h>
#endif
}

// Custom packet stream headers

// Define this for the max length of a ssid, not counting os-trailing null
#define MAX_SSID_LEN 32

// Define this for wlan-ng DLT_PRISM_HEADER support
#define WLAN_DEVNAMELEN_MAX 16

// The BSD datalink that doesn't report a sane value
#define KDLT_BSD802_11 -100

#ifndef DLT_PRISM_HEADER
#define DLT_PRISM_HEADER        119 /* prism header, not defined on some platforms */
#endif

#ifndef DLT_IEEE802_11_RADIO
#define	DLT_IEEE802_11_RADIO	127	/* 802.11 plus WLAN header */
#endif

#ifndef DLT_IEEE802_11_RADIO_AVS
#define DLT_IEEE802_11_RADIO_AVS	163 /* 802.11 plus AVS header, alternate link type */
#endif

#ifndef DLT_PPI
#define DLT_PPI						192 /* cace PPI */
#endif

// Extension to radiotap header not yet included in all BSD's
#ifndef IEEE80211_RADIOTAP_F_FCS
#define IEEE80211_RADIOTAP_F_FCS        0x10    /* frame includes FCS */
#endif

#ifndef IEEE80211_IOC_CHANNEL
#define IEEE80211_IOC_CHANNEL 0
#endif


// Generic pcapsource
class PcapSource : public KisPacketSource {
public:
    PcapSource(string in_name, string in_dev) : KisPacketSource(in_name, in_dev) { 
		decode_fcs = 0;
		crc32_table = NULL;
	}

    virtual int OpenSource();
    virtual int CloseSource();

    virtual int FetchDescriptor();

    virtual int FetchPacket(kis_packet *packet, uint8_t *data, uint8_t *moddata);

    static void Callback(u_char *bp, const struct pcap_pkthdr *header,
                         const u_char *in_data);

	virtual void SetSmartCRC(int in_smart);
protected:
    // Prism 802.11 headers from wlan-ng tacked on to the beginning of a
    // pcap packet... Snagged from the wlan-ng source
    typedef struct {
        uint32_t did;
        uint16_t status;
        uint16_t len;
        uint32_t data;
    } __attribute__((__packed__)) p80211item_uint32_t;

    typedef struct {
        uint32_t msgcode;
        uint32_t msglen;
        uint8_t devname[WLAN_DEVNAMELEN_MAX];
        p80211item_uint32_t hosttime;
        p80211item_uint32_t mactime;
        p80211item_uint32_t channel;
        p80211item_uint32_t rssi;
        p80211item_uint32_t sq;
        p80211item_uint32_t signal;
        p80211item_uint32_t noise;
        p80211item_uint32_t rate;
        p80211item_uint32_t istx;
        p80211item_uint32_t frmlen;
    }  __attribute__((__packed__)) wlan_ng_prism2_header;

    // Prism 802.11 headers from the openbsd Hermes drivers, even though they don't return
    // a valid linktype yet.  Structure lifted from bsd_airtools by dachb0den labs.
    typedef struct {
        u_int16_t wi_status;
        u_int16_t wi_ts0;
        u_int16_t wi_ts1;
        u_int8_t  wi_silence;
        u_int8_t  wi_signal;
        u_int8_t  wi_rate;
        u_int8_t  wi_rx_flow;
        u_int16_t wi_rsvd0;
        u_int16_t wi_rsvd1;
    } bsd_80211_header;

    // wlan-ng (and hopefully others) AVS header, version one.  Fields in
    // network byte order.
    typedef struct {
        uint32_t version;
        uint32_t length;
        uint64_t mactime;
        uint64_t hosttime;
        uint32_t phytype;
        uint32_t channel;
        uint32_t datarate;
        uint32_t antenna;
        uint32_t priority;
        uint32_t ssi_type;
        int32_t ssi_signal;
        int32_t ssi_noise;
        uint32_t preamble;
        uint32_t encoding;
    } avs_80211_1_header;

	typedef struct {
		uint8_t pph_version;
		uint8_t pph_flags;
		uint16_t pph_len;
		uint32_t pph_dlt;
	} ppi_packet_header;

#define PPI_PH_FLAG_ALIGNED		2

	typedef struct {
		uint16_t pfh_datatype;
		uint16_t pfh_datalen;
	} ppi_field_header;

#define PPI_FIELD_11COMMON		2
#define PPI_FIELD_11NMAC		3
#define PPI_FIELD_11NMACPHY		4
#define PPI_FIELD_SPECMAP		5
#define PPI_FIELD_PROCINFO		6
#define PPI_FIELD_CAPINFO		7

	typedef struct {
		uint16_t pfh_datatype;
		uint16_t pfh_datalen;
		uint64_t tsf_timer;
		uint16_t flags;
		uint16_t rate;
		uint16_t freq_mhz;
		uint16_t chan_flags;
		uint8_t fhss_hopset;
		uint8_t fhss_pattern;
		int8_t signal_dbm;
		int8_t noise_dbm;
	} ppi_80211_common;

#define PPI_80211_FLAG_FCS			1
#define PPI_80211_FLAG_TSFMS		2
#define PPI_80211_FLAG_INVALFCS		4
#define PPI_80211_FLAG_PHYERROR		8

#define PPI_80211_CHFLAG_TURBO 		16
#define PPI_80211_CHFLAG_CCK		32
#define PPI_80211_CHFLAG_OFDM		64
#define PPI_80211_CHFLAG_2GHZ		128
#define PPI_80211_CHFLAG_5GHZ		256
#define PPI_80211_CHFLAG_PASSIVE	512
#define PPI_80211_CHFLAG_DYNAMICCCK	1024
#define PPI_80211_CHFLAG_GFSK		2048

	typedef struct {
		uint16_t pfh_datatype;
		uint16_t pfh_datalen;
		uint32_t flags;
		uint32_t a_mpdu_id;
		uint8_t num_delimiters;
		uint8_t reserved[3];
	} ppi_11n_mac;

#define PPI_11NMAC_GREENFIELD		1
#define PPI_11NMAC_HT2040			2
#define PPI_11NMAC_RX_SGI			4
#define PPI_11NMAC_DUPERX			8
#define PPI_11NMAC_AGGREGATE		16
#define PPI_11NMAC_MOREAGGREGATE	32
#define PPI_11NMAC_AGGCRC			64

	typedef struct {
		uint16_t pfh_datatype;
		uint16_t pfh_datalen;
		uint32_t flags;
		uint32_t a_mpdu_id;
		uint8_t num_delimiters;
		uint8_t mcs;
		uint8_t num_streams;
		uint8_t combined_rssi;
		uint8_t ant0_ctl_rssi;
		uint8_t ant1_ctl_rssi;
		uint8_t ant2_ctl_rssi;
		uint8_t ant3_ctl_rssi;
		uint8_t ant0_ext_rssi;
		uint8_t ant1_ext_rssi;
		uint8_t ant2_ext_rssi;
		uint8_t ant3_ext_rssi;
		uint16_t extension_freq_mhz;
		uint16_t extension_flags;
		int8_t ant0_signal_dbm;
		int8_t ant0_noise_dbm;
		int8_t ant1_signal_dbm;
		int8_t ant1_noise_dbm;
		int8_t ant2_signal_dbm;
		int8_t ant2_noise_dbm;
		int8_t ant3_signal_dbm;
		int8_t ant3_noise_dbm;
		uint32_t evm0;
		uint32_t evm1;
		uint32_t evm2;
		uint32_t evm3;
	} ppi_11n_macphy;

    // Carrier setting
    virtual carrier_type IEEE80211Carrier();
    // Datalink checker
    virtual int DatalinkType();
    // Signal level fetcher
    virtual int FetchSignalLevels(int *in_siglev, int *in_noiselev);
    // Mangler
    virtual int ManglePacket(kis_packet *packet, uint8_t *data, uint8_t *moddata);
    // Mangle a prism2 datalink to a kismet packet
    int Prism2KisPack(kis_packet *packet, uint8_t *data, uint8_t *moddata);
    // Mangle a BSD header
    int BSD2KisPack(kis_packet *packet, uint8_t *data, uint8_t *moddata);
    // Mangle a radiotap header
#ifdef HAVE_RADIOTAP
    int Radiotap2KisPack(kis_packet *packet, uint8_t *data, uint8_t *moddata);
#endif
	int PPI2KisPack(kis_packet *packet, uint8_t *data, uint8_t *moddata);
    
    pcap_t *pd;
    int datalink_type;

	// Do we do anything intelligent with the FCS?
	int decode_fcs;
	unsigned int *crc32_table;

    // Some toggles for subclasses to use
    int toggle0;
    int toggle1;
};

// Open with pcap_dead for pcapfiles - we have a different open and we
// have to kluge fetching the packet descriptor
class PcapSourceFile : public PcapSource {
public:
    PcapSourceFile(string in_name, string in_dev) : PcapSource(in_name, in_dev) { }
    int OpenSource();
    int FetchPacket(kis_packet *packet, uint8_t *data, uint8_t *moddata);
    // int FetchDescriptor();
};

#ifdef SYS_LINUX
// What we need to track on a linux interface to restore the settings
typedef struct linux_ifparm {
    int flags;
    char essid[MAX_SSID_LEN + 1];
    int channel;
    int mode;
    int privmode;
    int prismhdr;
	void *power; // Most things won't use this
};
#endif

// Wireless extention pcapsource - use wext to get the channels.  Everything
// else is straight pcapsource
#ifdef HAVE_LINUX_WIRELESS
class PcapSourceWext : public PcapSource {
public:
    PcapSourceWext(string in_name, string in_dev) : PcapSource(in_name, in_dev) { 
        modern_chancontrol = -1;
    }

    // Small tracker var for intelligent channel control in orinoco.  I don't want to make
    // a new class for 1 int
    int modern_chancontrol;
protected:
    virtual int FetchSignalLevels(int *in_siglev, int *in_noiselev);
};

// FCS trimming for wext cards
class PcapSourceWextFCS : public PcapSourceWext {
public:
    PcapSourceWextFCS(string in_name, string in_dev) :
        PcapSourceWext(in_name, in_dev) { 
            fcsbytes = 4;
        }
};

// Override carrier detection for 11g cards like madwifi and prism54g.
class PcapSource11G : public PcapSourceWext {
public:
    PcapSource11G(string in_name, string in_dev) : 
        PcapSourceWext(in_name, in_dev) { }
protected:
    carrier_type IEEE80211Carrier();
};

// Override madwifi 11g for FCS
class PcapSource11GFCS : public PcapSource11G {
public:
    PcapSource11GFCS(string in_name, string in_dev) :
        PcapSource11G(in_name, in_dev) { 
            fcsbytes = 4;
        }
};
#endif

#ifdef SYS_LINUX
// Override fcs controls to add 4 bytes on wlanng
class PcapSourceWlanng : public PcapSource {
public:
    PcapSourceWlanng(string in_name, string in_dev) :
        PcapSource(in_name, in_dev) { 
            fcsbytes = 4;
        }
protected:
    // Signal levels are pulled from the prism2 or avs headers so leave that as 0
    int last_channel;

    friend int chancontrol_wlanng_avs(const char *in_dev, int in_ch, char *in_err, 
                                      void *in_ext);
    friend int chancontrol_wlanng(const char *in_dev, int in_ch, char *in_err, 
                                  void *in_ext);
};

// Override packet fetching logic on this one to discard jumbo corrupt packets
// that it likes to generate
class PcapSourceWrt54g : public PcapSource {
public:
    PcapSourceWrt54g(string in_name, string in_dev) : PcapSource(in_name, in_dev) { 
        fcsbytes = 4;
    }
	int OpenSource();
    int FetchPacket(kis_packet *packet, uint8_t *data, uint8_t *moddata);
protected:
    carrier_type IEEE80211Carrier();
};
#endif

#ifdef SYS_OPENBSD
class PcapSourceOpenBSDPrism : public PcapSource {
public:
    PcapSourceOpenBSDPrism(string in_name, string in_dev) :
        PcapSource(in_name, in_dev) { }
};
#endif

#if (defined(HAVE_RADIOTAP) && (defined(SYS_NETBSD) || defined(SYS_OPENBSD) || defined(SYS_FREEBSD)))
class RadiotapBSD {
public:
    RadiotapBSD(const char *ifname);
    virtual ~RadiotapBSD();

    const char *geterror() const;

    bool monitor_enable(int initch);
    bool monitor_reset(int initch);
    bool chancontrol(int in_ch);

    bool getmediaopt(int& options, int& mode);
    bool setmediaopt(int options, int mode);
    bool getifflags(int& flags);
    bool setifflags(int value);
    bool get80211(int type, int& val, int len, u_int8_t *data);
    bool set80211(int type, int val, int len, u_int8_t *data);
private:
    void perror(const char *, ...);
    void seterror(const char *, ...);
    bool checksocket();

    int s;
    int prev_flags;
    int prev_options;
    int prev_mode;
    int prev_chan;
    char errstr[256];
    string ifname;
};

class PcapSourceRadiotap : public PcapSource {
public:
    PcapSourceRadiotap(string in_name, string in_dev) :
        PcapSource(in_name, in_dev) { }
    int OpenSource();
protected:
    bool CheckForDLT(int dlt);
};
#endif

// ----------------------------------------------------------------------------
// Registrant and control functions
KisPacketSource *pcapsource_registrant(string in_name, string in_device,
                                       char *in_err);
KisPacketSource *pcapsourcefcs_registrant(string in_name, string in_device,
                                       char *in_err);

KisPacketSource *pcapsource_file_registrant(string in_name, string in_device, 
                                            char *in_err);

#ifdef HAVE_LINUX_WIRELESS
KisPacketSource *pcapsource_wext_registrant(string in_name, string in_device, 
                                            char *in_err);
KisPacketSource *pcapsource_wextfcs_registrant(string in_name, string in_device,
                                               char *in_err);
KisPacketSource *pcapsource_ciscowifix_registrant(string in_name, string in_device, 
                                                  char *in_err);
KisPacketSource *pcapsource_11g_registrant(string in_name, string in_device,
                                           char *in_err);
KisPacketSource *pcapsource_11gfcs_registrant(string in_name, string in_device,
                                              char *in_err);
KisPacketSource *pcapsource_11gfcschk_registrant(string in_name, string in_device,
												 char *in_err);
#endif

#ifdef SYS_LINUX
KisPacketSource *pcapsource_wlanng_registrant(string in_name, string in_device,
                                              char *in_err);
KisPacketSource *pcapsource_wrt54g_registrant(string in_name, string in_device,
                                              char *in_err);
#endif

#ifdef SYS_OPENBSD
KisPacketSource *pcapsource_openbsdprism2_registrant(string in_name, string in_device,
                                                     char *in_err);
#endif

#if (defined(HAVE_LIBPCAP) && defined(SYS_DARWIN))
KisPacketSource *pcapsource_darwin_registrant(string in_name, string in_device,
											  char *in_err);
#endif

#ifdef HAVE_RADIOTAP
KisPacketSource *pcapsource_radiotap_registrant(string in_name, string in_device,
                                                char *in_err);
#endif

// Monitor activation
int unmonitor_pcapfile(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);

#ifdef HAVE_LINUX_WIRELESS
// Cisco (old) 
int monitor_cisco(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_cisco(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
// Cisco (new)
int monitor_cisco_wifix(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
// hostap prism2
int monitor_hostap(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_hostap(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
// orinoco
int monitor_orinoco(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_orinoco(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
// acx100
int monitor_acx100(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_acx100(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
// admtek
int monitor_admtek(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_admtek(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
// ar5k
int monitor_vtar5k(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
// Madwifi group of cards
int monitor_madwifi_a(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int monitor_madwifi_b(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int monitor_madwifi_g(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int monitor_madwifi_comb(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_madwifi(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
// prism54 needs to override the error messages it gets setting channels
int monitor_prism54g(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_prism54g(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
// Centrino
int monitor_ipw2100(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_ipw2100(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int monitor_ipw2200(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_ipw2200(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int monitor_ipw3945(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_ipw3945(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int monitor_ipwlivetap(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_ipwlivetap(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
// Nokia needs to set power mgmt off and restore it
int monitor_nokia(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_nokia(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
// "Standard" wext monitor sequence - mostly a helper for other functions
// since most cards that use wext still have custom initialization that
// needs to be done.
int monitor_wext(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_wext(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
#endif

#ifdef SYS_LINUX
// wlan-ng modern standard
int monitor_wlanng(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
// wlan-ng avs
int monitor_wlanng_avs(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
// linksys wrt54g monitoring
int monitor_wrt54g(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_wrt54g(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
#endif

// This should be expanded to handle BSD...
#ifdef SYS_OPENBSD
// openbsd prism2
int monitor_openbsd_prism2(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
#endif

// Channel controls
#ifdef HAVE_LINUX_WIRELESS
// Standard wireless extension controls
int chancontrol_wext(const char *in_dev, int in_ch, char *in_err, void *in_ext);
// Orinoco iwpriv control
int chancontrol_orinoco(const char *in_dev, int in_ch, char *in_err, void *in_ext);
// Madwifi needs to set mode
int chancontrol_madwifi_ab(const char *in_dev, int in_ch, char *in_err, void *in_ext);
int chancontrol_madwifi_ag(const char *in_dev, int in_ch, char *in_err, void *in_ext);
// Prism54 apparently returns a fail code on an iwconfig channel change but
// then works so we need to override the wext failure code
int chancontrol_prism54g(const char *in_dev, int in_ch, char *in_err, void *in_ext);
// We need a delay in it like orinoco, apparently
int chancontrol_ipw2100(const char *in_dev, int in_ch, char *in_err, void *in_ext);
int chancontrol_ipw2200(const char *in_dev, int in_ch, char *in_err, void *in_ext);
#endif

#ifdef SYS_LINUX
// Modern wlan-ng and wlan-ng avs
int chancontrol_wlanng(const char *in_dev, int in_ch, char *in_err, void *in_ext);
int chancontrol_wlanng_avs(const char *in_dev, int in_ch, char *in_err, void *in_ext);
#endif

#ifdef SYS_OPENBSD
// openbsd prism2 controls
int chancontrol_openbsd_prism2(const char *in_dev, int in_ch, char *in_err, 
                               void *in_ext);
#endif

#if (defined(HAVE_RADIOTAP) && (defined(SYS_NETBSD) || defined(SYS_OPENBSD) || defined(SYS_FREEBSD)))
int monitor_bsd(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_bsd(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int chancontrol_bsd(const char *in_dev, int in_ch, char *in_err, void *in_ext);
#endif

#ifdef SYS_DARWIN
int monitor_darwin(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int unmonitor_darwin(const char *in_dev, int initch, char *in_err, void **in_if, void *in_ext);
int chancontrol_darwin(const char *in_dev, int in_ch, char *in_err, void *in_ext);
#endif

#endif

#endif
