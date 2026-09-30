/* Offline regression tests: no QRTR socket or modem is opened. */
#define main qmi_cli_main
#include "qmi-qrtr.c"
#undef main
#include <assert.h>

int main(void)
{
	uint8_t frame[64], tlvs[64];
	uint8_t result[4] = { 0 };
	uint8_t ipv4[4] = { 4, 3, 2, 1 }; /* Synthetic LE 0x01020304. */
	char address[16];
	size_t tl, len;
	uint16_t n;
	const uint8_t *v;

	format_ipv4(address, ipv4);
	assert(strcmp(address, "1.2.3.4") == 0);
	tl = qmi_tlv_put(tlvs, 0, 0x02, result, sizeof(result));
	len = qmi_build(frame, 1, 0x002d, tlvs, tl);
	assert(len == 14);
	frame[0] = QMI_RESP;
	assert(qmi_response_matches(frame, len, 1, 0x002d));
	assert(!qmi_response_matches(frame, len, 2, 0x002d));
	assert(!qmi_response_matches(frame, len, 1, 0x0020));
	assert(!qmi_response_matches(frame, len - 1, 1, 0x002d));
	assert(!qmi_response_matches(frame, 6, 1, 0x002d));
	frame[0] = QMI_IND;
	assert(!qmi_response_matches(frame, len, 1, 0x002d));
	frame[0] = QMI_RESP;
	assert(qmi_success(frame, len));
	v = qmi_find_tlv(frame, len, 0x02, &n);
	assert(v && n == 4);
	assert(!qmi_find_tlv(frame, len, 0x01, &n) && n == 0);
	frame[10] = 1;
	assert(!qmi_success(frame, len));
	frame[10] = 0;
	frame[8] = 5;
	assert(!qmi_find_tlv(frame, len, 0x02, &n));
	frame[8] = 4;
	frame[len] = 0x01;
	frame[5]++;
	assert(!qmi_find_tlv(frame, len + 1, 0x02, &n));
	g_txn = 255;
	assert(next_txn() == 255);
	assert(next_txn() == 1);
	{
		/* Exact native Android ONLINE request, txn 22, capture line 2635. */
		const uint8_t observed[] = {
			0, 22, 0, 0x2e, 0, 12, 0,
			1, 1, 0, 0, 0x10, 5, 0, 0, 0, 0, 0, 0
		};
		tl = dms_online_observed_tlvs(tlvs);
		assert(tl == 12);
		len = qmi_build(frame, 22, 0x002e, tlvs, tl);
		assert(len == sizeof(observed));
		assert(memcmp(frame, observed, sizeof(observed)) == 0);
		v = qmi_find_tlv(frame, len, 0x01, &n);
		assert(v && n == 1 && v[0] == 0);
		v = qmi_find_tlv(frame, len, 0x10, &n);
		assert(v && n == 5 && !memcmp(v, "\0\0\0\0\0", 5));
	}
	tl = wds_bind_tlvs(tlvs, 4, 1, 2);
	assert(tl == 15);
	assert(tlvs[0] == 0x10 && rd16(tlvs + 1) == 8);
	assert(rd32(tlvs + 3) == 4 && rd32(tlvs + 7) == 1);
	assert(tlvs[11] == 0x11 && rd16(tlvs + 12) == 1 && tlvs[14] == 2);
	{
		/* EMBEDDED=4, measured endpoint=1, modem RX=2/TX=23. */
		const uint8_t expected[] = {
			0x11, 0x11, 0, 1, 4, 0, 0, 0, 1, 0, 0, 0,
			2, 0, 0, 0, 23, 0, 0, 0
		};
		tl = dpm_open_tlvs(tlvs, 4, 1, 2, 23);
		assert(tl == sizeof(expected));
		assert(memcmp(tlvs, expected, tl) == 0);
		len = qmi_build(frame, 1, 0x0020, tlvs, tl);
		assert(len == 27 && rd16(frame + 5) == 20);
		tl = wda_get_tlvs(tlvs, 4, 1);
		assert(tl == 11 && tlvs[0] == 0x10 && rd16(tlvs + 1) == 8);
		assert(rd32(tlvs + 3) == 4 && rd32(tlvs + 7) == 1);
		tl = wda_qmap_tlvs(tlvs, 4, 1);
		assert(tl == 36);
		len = qmi_build(frame, 1, 0x0020, tlvs, tl);
		frame[0] = QMI_RESP;
		assert(wda_is_plain_qmap(frame, len));
		v = qmi_find_tlv(frame, len, 0x17, &n);
		assert(v && n == 8 && rd32(v) == 4 && rd32(v + 4) == 1);
		frame[10] = 1; /* QoS unexpectedly enabled. */
		assert(!wda_is_plain_qmap(frame, len));
		frame[10] = 0;
		frame[14] = 1; /* Ethernet instead of raw-IP. */
		assert(!wda_is_plain_qmap(frame, len));
	}
	{
		uint32_t value;
		assert(parse_u32("4294967295", &value) == 0 && value == UINT32_MAX);
		assert(parse_u32("4294967296", &value) < 0);
		assert(parse_u32("-1", &value) < 0);
		assert(parse_u32("4junk", &value) < 0);
		assert(parse_u32("", &value) < 0);
	}
	{
		const char *path = "msm/adsp/audio_pd";
		uint8_t listener[71];
		char longest[65];
		size_t plen = strlen(path);

		tl = servreg_listener_tlvs(listener, path, 1);
		assert(tl == 7 + plen);
		assert(listener[0] == 1 && rd16(listener + 1) == 1 && listener[3] == 1);
		assert(listener[4] == 2 && rd16(listener + 5) == plen);
		assert(memcmp(listener + 7, path, plen) == 0);
		tl = servreg_listener_tlvs(listener, path, 0);
		assert(tl == 7 + plen && listener[3] == 0);
		memset(longest, 'x', sizeof(longest) - 1);
		longest[64] = '\0';
		assert(servreg_listener_tlvs(listener, longest, 1) == sizeof(listener));
	}
	puts("PASS: QMI identity/result/TLVs, IPv4, transactions, DPM/WDA, servreg");
	return 0;
}
