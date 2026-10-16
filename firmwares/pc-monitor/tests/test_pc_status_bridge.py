#!/usr/bin/env python3
"""Host tests for the PC status bridge's pure logic and safety guards."""

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]

# The bridge imports pyserial at module load, so skip cleanly where it is absent
# (for example the CI static-checks job, which does not install pyserial).
if importlib.util.find_spec("serial") is None:
    print("SKIP: pyserial is not installed; bridge tests skipped")
    raise SystemExit(0)

SPEC = importlib.util.spec_from_file_location(
    "pc_status_bridge", ROOT / "tools" / "pc_status_bridge.py"
)
assert SPEC and SPEC.loader
BRIDGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BRIDGE)


class _FakePort:
    def __init__(self, device: str, vid: int | None = None, description: str = "") -> None:
        self.device = device
        self.vid = vid
        self.description = description


class SanitizeTest(unittest.TestCase):
    def test_replaces_frame_separators(self):
        self.assertEqual(BRIDGE.sanitize("a,b#c=d"), "a_b_c_d")

    def test_strips_and_truncates(self):
        self.assertEqual(BRIDGE.sanitize("  spaced  "), "spaced")
        self.assertEqual(BRIDGE.sanitize("x" * 100, 10), "x" * 10)


class ParseRequestTest(unittest.TestCase):
    def test_plain_request(self):
        self.assertEqual(BRIDGE.parse_request(b"$REQ#\r"), (None, 0))

    def test_need_and_sort(self):
        self.assertEqual(
            BRIDGE.parse_request(b"$REQ,need=proc,sort=1#\n"), ("proc", 1)
        )

    def test_need_without_sort(self):
        self.assertEqual(BRIDGE.parse_request(b"$REQ,need=port#"), ("port", 0))
        self.assertEqual(BRIDGE.parse_request(b"$REQ,need=gpu#"), ("gpu", 0))

    def test_non_request_lines(self):
        self.assertIsNone(BRIDGE.parse_request(b"I (123) boot: ready\r"))
        self.assertIsNone(BRIDGE.parse_request(b""))


class FrameTest(unittest.TestCase):
    def test_status_frame(self):
        self.assertEqual(
            BRIDGE.build_frame([("cpu", "12.5"), ("mem", "40.0")]),
            b"$PCSTAT,cpu=12.5,mem=40.0#\n",
        )

    def test_agent_frame_reports_psutil(self):
        with patch.object(BRIDGE, "psutil", object()):
            self.assertEqual(
                BRIDGE.build_agent_frame(), b"$PCAGENT,ver=5,psutil=1#\n"
            )
        with patch.object(BRIDGE, "psutil", None):
            self.assertEqual(
                BRIDGE.build_agent_frame(), b"$PCAGENT,ver=5,psutil=0#\n"
            )

    def test_proc_frame(self):
        frame = BRIDGE.build_proc_frame([("chrome", 12.3, 400.0)], 1)
        self.assertEqual(frame, b"$PCPROC,by=1,cnt=1,p0=chrome,u0=12.3,m0=400#\n")

    def test_port_frame(self):
        frame = BRIDGE.build_port_frame([(8000, "python")], 3)
        self.assertEqual(frame, b"$PCPORT,cnt=1,est=3,t0=8000,n0=python#\n")

    def test_gpu_frame(self):
        frame = BRIDGE.build_gpu_frame([("v", "nvidia"), ("u", "23.0")])
        self.assertEqual(frame, b"$PCGPU,v=nvidia,u=23.0#\n")


class ArkFrameTest(unittest.TestCase):
    def test_frame_is_utf8_for_chinese_name(self):
        frame = BRIDGE.build_ark_frame(
            [("err", "0"), ("plan", "Pro"), ("name", "闫玉良"), ("hq", "10.0")]
        )
        self.assertEqual(
            frame, "$PCARK,err=0,plan=Pro,name=闫玉良,hq=10.0#\n".encode("utf-8")
        )

    def test_sanitize_utf8_truncates_by_bytes_without_splitting(self):
        # 闫玉良 is 9 bytes; a 4-byte budget must keep only 闫 (3 bytes).
        self.assertEqual(BRIDGE.sanitize_utf8("闫玉良", 4), "闫")
        self.assertEqual(BRIDGE.sanitize_utf8("闫玉良", 15), "闫玉良")
        self.assertEqual(BRIDGE.sanitize_utf8("a,b#c", 15), "a_b_c")

    def test_need_ark_request_parses(self):
        self.assertEqual(BRIDGE.parse_request(b"$REQ,need=ark#"), ("ark", 0))


class ArkUsageSamplerTest(unittest.TestCase):
    def _write_config(self, directory: str, data: dict) -> str:
        import json

        path = Path(directory) / "ark_config.json"
        path.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
        return str(path)

    def test_missing_config_reports_not_configured(self):
        sampler = BRIDGE.ArkUsageSampler("no/such/ark_config.json")
        with patch.dict("os.environ", {}, clear=True):
            self.assertEqual(sampler.sample(), [("err", "1")])

    def test_empty_keys_report_not_configured_but_keep_tags(self):
        with tempfile.TemporaryDirectory(prefix="ark-tests-") as directory:
            path = self._write_config(
                directory,
                {"title_tag": "Coding Plan", "plan_tag": "Pro", "name_tag": "闫玉良"},
            )
            sampler = BRIDGE.ArkUsageSampler(path)
            with patch.dict("os.environ", {}, clear=True):
                fields = sampler.sample()
        self.assertEqual(
            fields,
            [("err", "1"), ("t", "Coding Plan"), ("plan", "Pro"), ("name", "闫玉良")],
        )

    def test_static_fields_title_sanitized_and_byte_capped(self):
        static = BRIDGE.ArkUsageSampler._static_fields(
            {"title_tag": "a,b#c" + "x" * 40}
        )
        self.assertEqual(static, [("t", "a_b_c" + "x" * 26)])

    def test_fields_from_payload(self):
        import time

        now_ms = time.time() * 1000.0
        payload = {
            "Result": {
                "PlanType": "Pro",
                "AFPFiveHour": {
                    "Quota": 25000,
                    "Used": 1234.5,
                    "ResetTime": now_ms + 600_000,
                },
                "AFPWeekly": {"Quota": 87500, "Used": 500},
                "AFPMonthly": {"Quota": 250000, "Used": 12000, "ResetTime": 0},
            }
        }
        sampler = BRIDGE.ArkUsageSampler("unused")
        fields = dict(sampler._fields_from_payload(payload, []))
        self.assertEqual(fields["err"], "0")
        self.assertEqual(fields["plan"], "Pro")
        self.assertEqual(fields["hq"], "25000.0")
        self.assertEqual(fields["hu"], "1234.5")
        self.assertTrue(590 <= int(fields["hr"]) <= 600)
        self.assertEqual(fields["wq"], "87500.0")
        self.assertEqual(fields["wu"], "500.0")
        self.assertNotIn("wr", fields)  # no ResetTime -> no reset field
        self.assertEqual(fields["mr"], "0")  # past reset clamps to 0

    def test_static_plan_tag_overrides_api_plan(self):
        payload = {"Result": {"PlanType": "Pro", "AFPWeekly": {"Quota": 1}}}
        sampler = BRIDGE.ArkUsageSampler("unused")
        static = BRIDGE.ArkUsageSampler._static_fields({"plan_tag": "Team"})
        fields = dict(sampler._fields_from_payload(payload, static))
        self.assertEqual(fields["plan"], "Team")

    def test_signed_headers_shape(self):
        import datetime

        config = {
            "access_key": "AKTEST",
            "secret_key": "SKTEST",
        }
        sampler = BRIDGE.ArkUsageSampler("unused")
        moment = datetime.datetime(2024, 1, 2, 3, 4, 5, tzinfo=datetime.timezone.utc)
        headers = sampler._signed_headers(config, b"{}", now=moment)
        self.assertEqual(headers["Host"], "ark.cn-beijing.volces.com")
        self.assertEqual(headers["X-Date"], "20240102T030405Z")
        self.assertEqual(
            headers["X-Content-Sha256"],
            "44136fa355b3678a1146ad16f7e8649e94fb4fc21fe77e8310c060f61caaff8a",
        )
        auth = headers["Authorization"]
        self.assertTrue(
            auth.startswith(
                "HMAC-SHA256 Credential=AKTEST/20240102/cn-beijing/ark/request, "
                "SignedHeaders=host;x-content-sha256;x-date, Signature="
            )
        )
        signature = auth.rsplit("Signature=", 1)[1]
        self.assertEqual(len(signature), 64)
        int(signature, 16)  # must be hex

    def test_signed_headers_empty_without_keys(self):
        sampler = BRIDGE.ArkUsageSampler("unused")
        with patch.dict("os.environ", {}, clear=True):
            self.assertEqual(sampler._signed_headers({}, b"{}"), {})

    def test_env_keys_override_file(self):
        with patch.dict(
            "os.environ",
            {"VOLC_ACCESSKEY": "ENV_AK", "VOLC_SECRETKEY": "ENV_SK"},
        ):
            ak, sk = BRIDGE.ArkUsageSampler._keys(
                {"access_key": "FILE_AK", "secret_key": "FILE_SK"}
            )
        self.assertEqual((ak, sk), ("ENV_AK", "ENV_SK"))

    def test_fetch_error_reports_err2_with_tags(self):
        with tempfile.TemporaryDirectory(prefix="ark-tests-") as directory:
            path = self._write_config(
                directory,
                {
                    "access_key": "AK",
                    "secret_key": "SK",
                    "name_tag": "闫玉良",
                },
            )
            sampler = BRIDGE.ArkUsageSampler(path)
            with patch.dict("os.environ", {}, clear=True):
                with patch.object(
                    sampler, "_fetch", side_effect=RuntimeError("boom")
                ):
                    fields = sampler.sample()
        self.assertEqual(fields, [("err", "2"), ("name", "闫玉良")])


class LhmSelectTest(unittest.TestCase):
    @staticmethod
    def _tree(*sensors):
        """Build a minimal LHM data.json tree from (id, type, text, value)."""
        return {
            "Text": "Sensor",
            "Children": [
                {
                    "Text": text,
                    "Type": sensor_type,
                    "SensorId": sensor_id,
                    "Value": value,
                    "Children": [],
                }
                for sensor_id, sensor_type, text, value in sensors
            ],
        }

    def test_prefers_cpu_package_temperature(self):
        root = self._tree(
            ("/intelcpu/0/temperature/0", "Temperature", "Core Max", "61.0 °C"),
            ("/intelcpu/0/temperature/26", "Temperature", "CPU Package", "72.5 °C"),
        )
        cpu_temp, gpu_fan = BRIDGE.LhmSensors.select(root)
        self.assertEqual(cpu_temp, 72.5)
        self.assertIsNone(gpu_fan)

    def test_falls_back_to_any_cpu_temperature(self):
        root = self._tree(
            ("/amdcpu/0/temperature/2", "Temperature", "Core (Tctl/Tdie)", "55.0 °C"),
        )
        cpu_temp, _ = BRIDGE.LhmSensors.select(root)
        self.assertEqual(cpu_temp, 55.0)

    def test_parses_culture_dependent_decimal_comma(self):
        root = self._tree(
            ("/intelcpu/0/temperature/26", "Temperature", "CPU Package", "66,5 °C"),
        )
        cpu_temp, _ = BRIDGE.LhmSensors.select(root)
        self.assertEqual(cpu_temp, 66.5)

    def test_gpu_fan_control_percent(self):
        root = self._tree(
            ("/lpc/ec/0/control/0", "Control", "CPU Fan", "43.0 %"),
            ("/gpu-nvidia/0/control/0", "Control", "GPU Fan", "51.0 %"),
        )
        _, gpu_fan = BRIDGE.LhmSensors.select(root)
        self.assertEqual(gpu_fan, 51.0)

    def test_unrelated_and_bad_sensors_are_ignored(self):
        root = self._tree(
            ("/gpu-nvidia/0/temperature/0", "Temperature", "GPU Core", "49.0 °C"),
            ("/lpc/ec/0/fan/0", "Fan", "GPU Fan", "2400.0 RPM"),
            ("/gpu-nvidia/0/control/0", "Control", "GPU Fan", "N/A"),
        )
        cpu_temp, gpu_fan = BRIDGE.LhmSensors.select(root)
        self.assertIsNone(cpu_temp)
        self.assertIsNone(gpu_fan)

    def test_empty_input(self):
        self.assertEqual(BRIDGE.LhmSensors.select({"Children": []}), (None, None))
        self.assertEqual(BRIDGE.LhmSensors.select({}), (None, None))


class DetectPortTest(unittest.TestCase):
    def _detect(self, ports, explicit=None):
        with patch.object(BRIDGE.list_ports, "comports", return_value=ports):
            return BRIDGE.detect_port(explicit)

    def test_explicit_port_wins(self):
        self.assertEqual(self._detect([], "COM42"), "COM42")

    def test_prefers_esp_vid(self):
        ports = [_FakePort("COM3"), _FakePort("COM7", vid=BRIDGE.ESP_VID)]
        self.assertEqual(self._detect(ports), "COM7")

    def test_single_port_is_used(self):
        self.assertEqual(self._detect([_FakePort("COM9")]), "COM9")

    def test_no_port_raises(self):
        with self.assertRaises(BRIDGE.PortUnavailable):
            self._detect([])

    def test_multiple_ports_raise(self):
        ports = [_FakePort("COM1"), _FakePort("COM2")]
        with self.assertRaises(BRIDGE.PortUnavailable):
            self._detect(ports)


class SingleInstanceTest(unittest.TestCase):
    def test_second_acquire_is_rejected_then_released(self):
        with tempfile.TemporaryDirectory(prefix="pcmon-bridge-tests-") as directory:
            path = str(Path(directory) / "pcmon-bridge.lock")
            first = BRIDGE.SingleInstance(path).acquire()
            try:
                with self.assertRaises(BRIDGE.AlreadyRunning):
                    BRIDGE.SingleInstance(path).acquire()
            finally:
                first.release()
            again = BRIDGE.SingleInstance(path).acquire()
            again.release()


class StatePathTest(unittest.TestCase):
    def test_default_paths(self):
        self.assertEqual(Path(BRIDGE.default_log_path()).name, "pcmon-bridge.log")
        self.assertEqual(Path(BRIDGE.default_lock_path()).name, "pcmon-bridge.lock")
        self.assertIn("FoloToy", BRIDGE.default_log_path())


if __name__ == "__main__":
    unittest.main(verbosity=2)
