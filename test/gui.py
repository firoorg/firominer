#!/usr/bin/env python3
"""Run python3 test/gui.py headlessly, or add --ui with Tk 8.6+ and a display."""
import importlib.util
from pathlib import Path
import queue
import subprocess
import sys
import threading
import unittest
from unittest.mock import Mock, patch

UI_TESTS = "--ui" in sys.argv
if UI_TESTS:
    sys.argv.remove("--ui")

spec = importlib.util.spec_from_file_location("firominer_gui", Path(__file__).resolve().parents[1] / "gui" / "firominer_gui.py")
gui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gui)


def device_table(kind="Gpu", cl="Yes", cuda=False, pci="01:00.0"):
    extra = "CUDA SM  " if cuda else ""
    header = " Id " + "Pci Id    " + "Type " + "Name                          " + extra + "CL   " + " Total Memory"
    row = "  0 " + pci.ljust(10) + kind.ljust(5) + "Example GPU".ljust(30) + ("Yes  8.6 " if cuda else "") + cl.ljust(5) + "      8.00 GB"
    return header + "\n" + row + "\n"


class GuiTests(unittest.TestCase):
    def command(self, **changes):
        values = dict(executable=sys.executable, mode="solo", endpoint="getwork://127.0.0.1:8888",
                      username="rpc.name", password="p.a:ss@word+%`", reward="aFiroRewardAddress")
        values.update(changes)
        return gui.build_command(**values)

    def test_commands(self):
        command = self.command(message="--hello 'Firo'", network="testnet")
        self.assertIn("--coinbase-message=--hello 'Firo'", command)
        self.assertIn("testnet", command)
        uri = command[command.index("-P") + 1]
        self.assertEqual(uri, "getwork://rpc%2Ename:p%2Ea%3Ass%40word%2B%25%60@127.0.0.1:8888")
        self.assertNotIn("-G", command)
        command = self.command(mode="pool", endpoint="stratum+tls://pool.example:4444", username="wallet.worker", inline=True, opencl=True)
        self.assertIn("-G", command)
        self.assertIn("--cl-experimental-inline", command)
        self.assertNotIn("--reward-address", command)
        self.assertTrue(any("wallet.worker:" in value for value in command))
        command = self.command(endpoint="http://localhost:8888/wallet@name+1")
        self.assertTrue(any(value.endswith("/wallet%40name%2B1") for value in command))
        self.command(message="é" * 40)

    def test_validation(self):
        with patch.object(gui.sys, "platform", "win32"), patch.object(gui.shutil, "which", return_value="miner.cmd"):
            with self.assertRaises(ValueError):
                gui.executable_path("miner.cmd")
        for changes in ({"message": "é" * 41}, {"reward": ""}, {"inline": True},
                        {"mode": "pool", "endpoint": "stratum://pool:42", "message": "tag"},
                        {"endpoint": "http://user:pass@localhost:42"},
                        {"endpoint": "http://[::1]:8888"}, {"endpoint": "http://localhost:0"},
                        {"endpoint": "http://localhost:65536"}, {"endpoint": "http://localhost"},
                        {"endpoint": "http://exit:42"}, {"endpoint": "https://localhost:42"},
                        {"endpoint": "http://localhost:42?x=1"}, {"username": ""},
                        {"endpoint": "http://localhost:42/a%20b"},
                        {"endpoint": "http://localhost:42/a%00b"},
                        {"password": "a\nb"}, {"password": "x" * 1024},
                        {"network": "unknown"}, {"executable": "/no/such/firominer"}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.command(**changes)

    def test_detection(self):
        self.assertTrue(gui.has_opencl_gpu(device_table()))
        self.assertTrue(gui.has_opencl_gpu(device_table("Acc", cuda=True)))
        self.assertTrue(gui.has_opencl_gpu(device_table(pci="ffffff83:00.0")))
        self.assertTrue(gui.has_opencl_gpu(device_table(cuda=True, pci="ffffff83:00.0")))
        self.assertTrue(gui.has_opencl_gpu("\x1b[31m" + device_table() + "\x1b[0m"))
        for text in ("", "No usable mining devices found", "Gpu Yes OpenCL", device_table("Cpu"),
                     device_table(cl=""), device_table(cl="", pci="ffffff83:00.0"),
                     " Id Pci Id Type Name CL\n malformed Gpu Yes"):
            self.assertFalse(gui.has_opencl_gpu(text))
        with patch.object(gui.subprocess, "run", return_value=Mock(returncode=0, stdout=device_table())) as run:
            self.assertTrue(gui.probe_opencl(sys.executable)[0])
            self.assertEqual(run.call_args.args[0][1:], ["-G", "--list-devices", "--nocolor"])
            self.assertNotIn("shell", run.call_args.kwargs)
        with patch.object(gui.subprocess, "run", side_effect=subprocess.TimeoutExpired("scan", 20)):
            self.assertFalse(gui.probe_opencl(sys.executable)[0])
        with patch.object(gui.subprocess, "run", return_value=Mock(returncode=1, stdout=device_table())):
            self.assertFalse(gui.probe_opencl(sys.executable)[0])

    def test_process_lifecycle_and_redaction(self):
        logs, events = queue.Queue(maxsize=10), queue.Queue()
        # A local Python child verifies pipes and stop/reaping without launching any mining work.
        process = subprocess.Popen([sys.executable, "-u", "-c", "import time; print('worker.secret p@ss'); print('a' * 8190 + 'p@ss'); time.sleep(30)"], **gui.process_options())
        reader = threading.Thread(target=gui.read_output, args=(process, logs, events, ("worker.secret", "p@ss")))
        reader.start()
        try:
            self.assertEqual(logs.get(timeout=5), "[hidden] [hidden]\n")
            self.assertEqual(logs.get(timeout=5), "[Oversized miner log line omitted]\n")
        finally:
            gui.stop_process(process)
            reader.join(timeout=5)
        self.assertFalse(reader.is_alive())
        event, owner, code = events.get(timeout=5)
        self.assertEqual(event, "exit")
        self.assertIs(owner, process)
        self.assertIsNotNone(code)
        gui.stop_process(process)  # Repeated stops are harmless.
        self.assertEqual(gui.redact("http://name:p%40ss@localhost:42 p@ss p%40ss", ("p@ss",)),
                         "http://[credentials]@localhost:42 [hidden] [hidden]")
        process = Mock()
        process.poll.return_value = None
        process.wait.side_effect = [subprocess.TimeoutExpired("miner", 5), 0]
        gui.stop_process(process)
        process.terminate.assert_called_once()
        process.kill.assert_called_once()


@unittest.skipUnless(UI_TESTS, "Add --ui to check the native window with Tk and a display")
class GuiWindowTests(unittest.TestCase):
    def test_window(self):
        import tkinter as tk
        from tkinter import ttk, filedialog
        self.assertGreaterEqual(tk.TkVersion, 8.6, "Use current Python with Tk 8.6+ for the window smoke test")
        root = tk.Tk()
        try:
            dialogs = Mock()
            with patch.multiple(gui, tk=tk, ttk=ttk, filedialog=filedialog, messagebox=dialogs, create=True), \
                    patch.object(gui, "find_miner", return_value=""), patch.object(gui.subprocess, "Popen") as launch:
                window = gui.MinerWindow(root)
                root.update()
                self.assertTrue(all(widget.winfo_ismapped() for widget in window.solo))
                self.assertEqual(window.solo[1].winfo_x(), window.connection_entries["username"].winfo_x())
                self.assertIn("disabled", window.inline_check.state())
                window.executable.set(sys.executable)
                window.start()
                dialogs.showerror.assert_called_once()
                launch.assert_not_called()
                window.connections["solo"]["username"].set("rpc-user")
                window.mode.set("pool")
                window.change_mode()
                root.update()
                self.assertFalse(any(widget.winfo_ismapped() for widget in window.solo))
                self.assertTrue(window.pool_note.winfo_ismapped())
                self.assertEqual(window.connections["pool"]["username"].get(), "")
                self.assertGreater(window.log.winfo_height(), 40)
                window.mode.set("solo")
                window.change_mode()
                root.geometry("760x700")
                root.update()
                self.assertGreater(window.log.winfo_height(), 40)
                if sys.platform == "darwin":
                    root.tk.call("::tk::mac::Quit")
                    self.assertTrue(window.closing)
                else:
                    window.close()
                window.poll()
                root = None
        finally:
            if root is not None:
                root.destroy()


if __name__ == "__main__":
    unittest.main()
