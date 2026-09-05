#!/usr/bin/env python3
"""Small desktop launcher for firominer. Requires Python 3.9+ and Tk."""

import os
from pathlib import Path
import queue
import re
import shutil
import subprocess
import sys
import threading
from urllib.parse import quote, unquote, urlsplit, urlunsplit


NETWORKS = ("mainnet", "testnet", "devnet", "regtest")
POOL_SCHEMES = {"stratum", "stratums", "stratumss"} | {
    "stratum" + version + "+" + transport
    for version in ("", "1", "2", "3")
    for transport in ("tcp", "tls", "tls12", "ssl")
}
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def find_miner():
    directory = Path(__file__).resolve().parent
    name = "firominer.exe" if os.name == "nt" else "firominer"
    candidates = [directory / name, directory.parent / name]
    for build in ("build", "build-release", "build-opencl", "build-cuda"):
        candidates.extend(directory.parent / build / suffix / name
                          for suffix in ("firominer", "firominer/Release", "bin", ""))
    return next((str(path) for path in candidates if path.is_file()
                 and os.access(path, os.X_OK)), shutil.which(name) or "")


def executable_path(value):
    path = Path(value).expanduser()
    if path.is_file() and os.access(path, os.X_OK):
        found = str(path.resolve())
    else:
        found = shutil.which(value) if value else None
    if found:
        if sys.platform == "win32" and Path(found).suffix.lower() != ".exe":
            raise ValueError("Choose firominer.exe, rather than a batch or command script.")
        return found
    raise ValueError("Choose a runnable firominer executable first.")


def build_command(executable, mode, endpoint, username, password, reward="",
                  message="", network="mainnet", inline=False, opencl=False):
    executable = executable_path(executable)
    if mode not in ("solo", "pool") or network not in NETWORKS:
        raise ValueError("Select a mining mode and network.")
    for value in (endpoint, username, password, reward, message):
        if any(ord(char) < 32 or ord(char) == 127 for char in value):
            raise ValueError("Settings cannot contain control characters or newlines.")
    try:
        url = urlsplit(endpoint.strip())
        port = url.port
        host = url.hostname
    except ValueError:
        raise ValueError("Enter an endpoint with a valid host and port (1–65535).") from None
    schemes = {"getwork", "http"} if mode == "solo" else POOL_SCHEMES
    if url.scheme not in schemes:
        raise ValueError("Solo requires getwork:// or http://; pools require a Stratum URL.")
    if not host or not port or any(char.isspace() for char in endpoint):
        raise ValueError("Enter an endpoint with an explicit host and port (1–65535).")
    if ":" in host:
        raise ValueError("Use an IPv4 address or hostname; this miner's URL parser does not support bracketed IPv6.")
    if not re.fullmatch(r"[A-Za-z0-9.-]+", host) or host.lower() == "exit":
        raise ValueError("Enter a valid node or pool hostname.")
    if url.username is not None or url.password is not None:
        raise ValueError("Enter credentials in the username and password fields, outside the endpoint URL.")
    if url.query or url.fragment:
        raise ValueError("Endpoint URLs cannot contain a query or fragment.")
    if any(char.isspace() or ord(char) < 32 or ord(char) == 127 for char in unquote(url.path)):
        raise ValueError("Endpoint paths cannot contain encoded whitespace or control characters.")
    if not username:
        raise ValueError("Enter the node RPC username or pool wallet / worker name.")
    # PoolURI splits dots before URL decoding; only pool usernames use a worker suffix.
    user = quote(username, safe="")
    if mode == "solo":
        user = user.replace(".", "%2E")
    secret = quote(password, safe="").replace(".", "%2E")
    uri = urlunsplit((url.scheme, user + ":" + secret + "@" + url.netloc,
                     quote(url.path, safe="/%"), "", ""))
    if len(uri.encode("utf-8")) > 1024:
        raise ValueError("Endpoint and encoded credentials must fit within 1024 bytes.")
    command = [executable, "--nocolor", "--stdout", "--firopow-network", network, "-P", uri]
    if mode == "solo":
        reward = reward.strip()
        if not reward or reward.startswith("-") or any(char.isspace() for char in reward):
            raise ValueError("Enter the Firo address that should receive your solo block reward.")
        command.extend(["--reward-address", reward])
        if len(message.encode("utf-8")) > 80:
            raise ValueError("The coinbase message must be at most 80 UTF-8 bytes.")
        if message:
            command.append("--coinbase-message=" + message)
    elif message:
        raise ValueError("Pools construct the coinbase; a custom message is available only for solo mining.")
    if inline:
        if not opencl:
            raise ValueError("Detect an OpenCL GPU with this executable before enabling the experiment.")
        command.extend(["-G", "--cl-experimental-inline"])
    return command


def has_opencl_gpu(output):
    """Read the miner's fixed-width table, never GPU-looking diagnostic prose."""
    columns = None
    for line in ANSI.sub("", output).splitlines():
        if re.match(r"\s*Id\s+Pci Id\s+Type\s+Name\s+", line):
            match = re.search(r"\bCL\s+", line)
            columns = match.start() - line.index("Type") if match else None
        elif columns is not None:
            row = re.match(r"\s*\d+\s+[0-9A-Fa-f:.]*\s*(Gpu|Acc)\s", line)
            if row:
                # setw sets minimum widths; a long PCI ID moves the remaining columns.
                cl = row.start(1) + columns
                if line[cl:cl + 5].strip() == "Yes":
                    return True
    return False


def process_options():
    options = dict(stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                   stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    if os.name == "nt":
        options["creationflags"] = subprocess.CREATE_NO_WINDOW
    return options


def probe_opencl(executable):
    try:
        result = subprocess.run([executable, "-G", "--list-devices", "--nocolor"],
                                timeout=20, **process_options())
        if result.returncode == 0 and has_opencl_gpu(result.stdout):
            return True, "OpenCL GPU detected · inline experiment available"
        return False, "No usable OpenCL GPU detected by this miner"
    except subprocess.TimeoutExpired:
        return False, "OpenCL scan timed out; check your GPU driver and retry"
    except OSError:
        return False, "Could not run the miner; check the executable and its runtime libraries"


def redact(text, secrets):
    text = ANSI.sub("", text)
    text = re.sub(r"(\b[a-zA-Z0-9+]+://)[^\s/]*@", r"\1[credentials]@", text)
    variants = {part for secret in secrets if secret
                for part in (secret, quote(secret, safe=""), quote(secret, safe="").replace(".", "%2E"))}
    for secret in sorted(variants, key=len, reverse=True):
        text = text.replace(secret, "[hidden]")
    return text


def read_output(process, logs, events, secrets):
    def emit(line):
        if line:
            try:
                logs.put_nowait(redact(line, secrets))
            except queue.Full:
                pass  # Keep mining responsive when the UI cannot keep up.
    try:
        with process.stdout:
            line, oversized = "", False
            for chunk in iter(lambda: process.stdout.readline(8192), ""):
                if not oversized:
                    line += chunk
                    if len(line) > 8192:
                        line, oversized = "[Oversized miner log line omitted]\n", True
                if chunk.endswith("\n"):
                    emit(line)
                    line, oversized = "", False
            emit(line)
        code = process.wait()
    except (OSError, ValueError):
        stop_process(process)
        code = process.poll()
    events.put(("exit", process, code))


def stop_process(process):
    try:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    except ProcessLookupError:
        pass


class MinerWindow:
    def __init__(self, root):
        self.root = root
        self.process = None
        self.stopping = False
        self.closing = False
        self.scanning = False
        self.opencl = False
        self.scanned_path = ""
        self.logs = queue.Queue(maxsize=500)
        self.events = queue.Queue()
        self.mode = tk.StringVar(value="solo")
        self.network = tk.StringVar(value="mainnet")
        self.executable = tk.StringVar(value=find_miner())
        self.connections = {mode: {name: tk.StringVar(value=value) for name, value in (
            ("endpoint", "getwork://127.0.0.1:8888" if mode == "solo" else "stratum+tcp://"),
            ("username", ""), ("password", ""))} for mode in ("solo", "pool")}
        self.reward = tk.StringVar()
        self.message = tk.StringVar()
        self.inline = tk.BooleanVar()
        self.status = tk.StringVar(value="Ready")
        self.hardware = tk.StringVar(value="Choose a miner, then scan for OpenCL GPUs")
        self.inputs = []
        root.title("Firo miner")
        root.geometry("820x760")
        root.minsize(760, 700)
        root.protocol("WM_DELETE_WINDOW", self.close)
        if sys.platform == "darwin":
            root.createcommand("::tk::mac::Quit", self.close)
        root.columnconfigure(0, weight=1)
        root.rowconfigure(0, weight=1)
        frame = ttk.Frame(root, padding=16)
        frame.grid(sticky="nsew")
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(5, weight=1)
        ttk.Label(frame, text="Firo miner", font=("TkDefaultFont", 23, "bold")).grid(sticky="w")
        ttk.Label(frame, text="Connect a Firo node or pool and put your GPU to work.").grid(row=1, sticky="w", pady=(4, 10))

        settings = ttk.LabelFrame(frame, text="Mining setup", padding=10)
        settings.grid(row=2, sticky="ew")
        settings.columnconfigure(1, weight=1)
        ttk.Label(settings, text="Miner executable").grid(row=0, column=0, sticky="w", padx=(0, 14))
        self.binary_entry = ttk.Entry(settings, textvariable=self.executable)
        self.binary_entry.grid(row=0, column=1, sticky="ew")
        self.browse_button = ttk.Button(settings, text="Browse…", command=self.browse)
        self.browse_button.grid(row=0, column=2, padx=(8, 0))
        self.inputs.extend([self.binary_entry, self.browse_button])
        choice = ttk.Frame(settings)
        choice.grid(row=1, column=0, columnspan=3, sticky="ew", pady=(10, 8))
        for mode, text in (("solo", "Solo mine"), ("pool", "Pool mine")):
            button = ttk.Radiobutton(choice, text=text, value=mode, variable=self.mode, command=self.change_mode)
            button.pack(side="left", padx=(0, 22))
            self.inputs.append(button)
        self.network_combo = ttk.Combobox(choice, textvariable=self.network, values=NETWORKS, state="readonly", width=11)
        self.network_combo.pack(side="right")
        ttk.Label(choice, text="Network  ").pack(side="right")
        self.connection_entries = {}
        self.connection_labels = {}
        for row, name in enumerate(("endpoint", "username", "password"), 2):
            label = ttk.Label(settings)
            label.grid(row=row, column=0, sticky="w", pady=5)
            entry = ttk.Entry(settings, show="•" if name == "password" else "")
            entry.grid(row=row, column=1, columnspan=2, sticky="ew", pady=5)
            self.connection_labels[name] = label
            self.connection_entries[name] = entry
            self.inputs.append(entry)
        self.solo = []
        for row, (text, variable) in enumerate((("Reward address", self.reward), ("Coinbase message", self.message)), 5):
            label = ttk.Label(settings, text=text)
            label.grid(row=row, column=0, sticky="w", pady=5, padx=(0, 14))
            entry = ttk.Entry(settings, textvariable=variable)
            entry.grid(row=row, column=1, columnspan=2, sticky="ew", pady=5)
            self.inputs.append(entry)
            self.solo.extend([label, entry])
        note = ttk.Label(settings, text="Optional message · maximum 80 UTF-8 bytes. Requires the companion-patched\nFiro node. Leave blank with a standard node.")
        note.grid(row=7, column=0, columnspan=3, sticky="w", pady=(3, 0))
        self.solo.append(note)
        self.pool_note = ttk.Label(settings, text="Your pool sets the reward and coinbase message.")
        self.pool_note.grid(row=8, column=0, columnspan=3, sticky="w", pady=(8, 0))

        hardware = ttk.LabelFrame(frame, text="GPU", padding=10)
        hardware.grid(row=3, sticky="ew", pady=8)
        hardware.columnconfigure(0, weight=1)
        self.inline_check = ttk.Checkbutton(hardware, text="Use the OpenCL inline experiment", variable=self.inline)
        self.inline_check.grid(row=0, sticky="w")
        self.scan_button = ttk.Button(hardware, text="Scan GPUs", command=self.scan)
        self.scan_button.grid(row=0, column=1, padx=(12, 0))
        ttk.Label(hardware, textvariable=self.hardware).grid(row=1, sticky="w", pady=(6, 0))
        ttk.Label(hardware, text="Experimental: forces OpenCL. Leave off for the normal GPU backend.").grid(row=2, columnspan=2, sticky="w", pady=(3, 0))

        actions = ttk.Frame(frame)
        actions.grid(row=4, sticky="ew", pady=(0, 8))
        self.start_button = ttk.Button(actions, text="Start mining", command=self.start)
        self.start_button.pack(side="left")
        self.stop_button = ttk.Button(actions, text="Stop", command=self.stop)
        self.stop_button.pack(side="left", padx=8)
        ttk.Label(actions, textvariable=self.status).pack(side="right")
        log_frame = ttk.LabelFrame(frame, text="Live miner log", padding=8)
        log_frame.grid(row=5, sticky="nsew")
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)
        self.log = tk.Text(log_frame, height=8, wrap="word", state="disabled", font="TkFixedFont", borderwidth=0)
        self.log.grid(sticky="nsew")
        scroll = ttk.Scrollbar(log_frame, command=self.log.yview)
        scroll.grid(row=0, column=1, sticky="ns")
        self.log.configure(yscrollcommand=scroll.set)
        self.executable.trace_add("write", self.invalidate_scan)
        self.change_mode()
        self.refresh()
        root.after(100, self.poll)
        if self.executable.get():
            root.after(200, self.scan)

    def change_mode(self):
        mode = self.mode.get()
        labels = ("Node endpoint", "RPC username", "RPC password") if mode == "solo" else ("Pool endpoint", "Wallet / worker", "Pool password")
        for name, label in zip(("endpoint", "username", "password"), labels):
            self.connection_labels[name].configure(text=label)
            self.connection_entries[name].configure(textvariable=self.connections[mode][name])
        if mode == "solo":
            for widget in self.solo:
                widget.grid()
            self.pool_note.grid_remove()
        else:
            for widget in self.solo:
                widget.grid_remove()
            self.pool_note.grid()

    def browse(self):
        path = filedialog.askopenfilename(title="Choose the firominer executable", parent=self.root)
        if path:
            self.executable.set(path)
            self.scan()

    def invalidate_scan(self, *_):
        self.opencl = False
        self.scanned_path = ""
        self.inline.set(False)
        self.hardware.set("Executable changed · scan to enable the OpenCL experiment")
        self.refresh()

    def refresh(self):
        busy = self.process is not None or self.closing
        for widget in self.inputs:
            widget.configure(state="disabled" if busy else "normal")
        self.network_combo.configure(state="disabled" if busy else "readonly")
        for widget in (self.binary_entry, self.browse_button, self.scan_button):
            widget.configure(state="disabled" if busy or self.scanning else "normal")
        self.inline_check.configure(state="normal" if self.opencl and not busy else "disabled")
        self.start_button.configure(state="disabled" if busy or self.scanning else "normal")
        self.stop_button.configure(state="normal" if self.process and not self.stopping else "disabled")

    def scan(self):
        if self.process or self.scanning or self.closing:
            return
        try:
            path = executable_path(self.executable.get())
        except ValueError as error:
            self.hardware.set(str(error))
            return
        self.opencl = False
        self.inline.set(False)
        self.scanning = True
        self.hardware.set("Scanning OpenCL GPUs…")
        self.refresh()
        def work():
            self.events.put(("scan", path, probe_opencl(path)))
        threading.Thread(target=work, daemon=True).start()

    def start(self):
        if self.process or self.scanning or self.closing:
            return
        values = {name: variable.get() for name, variable in self.connections[self.mode.get()].items()}
        try:
            command = build_command(self.executable.get(), self.mode.get(), **values,
                reward=self.reward.get(), message=self.message.get() if self.mode.get() == "solo" else "",
                network=self.network.get(), inline=self.inline.get(),
                opencl=self.opencl and self.scanned_path == executable_path(self.executable.get()))
            self.process = subprocess.Popen(command, bufsize=1, **process_options())
        except (ValueError, OSError) as error:
            # Never show CalledProcessError or a command line: it contains RPC credentials.
            messagebox.showerror("Cannot start mining", str(error) if isinstance(error, ValueError)
                                 else "Could not launch the miner. Check its executable and runtime libraries.", parent=self.root)
            return
        self.stopping = False
        self.status.set("Mining · " + self.mode.get() + " · " + self.network.get())
        self.append_log("\nStarted mining. Waiting for the miner to connect…\n")
        self.refresh()
        threading.Thread(target=read_output, args=(self.process, self.logs, self.events,
                         (values["username"], values["password"])), daemon=True).start()

    def stop(self):
        if self.process and not self.stopping:
            self.stopping = True
            self.status.set("Stopping…")
            self.refresh()
            process = self.process
            def work():
                try:
                    stop_process(process)
                except OSError:
                    self.events.put(("stop_error", process, None))
            threading.Thread(target=work, daemon=True).start()

    def append_log(self, text):
        at_bottom = self.log.yview()[1] >= 0.99
        self.log.configure(state="normal")
        self.log.insert("end", text)
        # ponytail: retain 1,000 log lines; add file logging only when session history is needed.
        extra = int(self.log.index("end-1c").split(".")[0]) - 1000
        if extra > 0:
            self.log.delete("1.0", str(extra + 1) + ".0")
        if at_bottom:
            self.log.see("end")
        self.log.configure(state="disabled")

    def poll(self):
        lines = []
        for _ in range(100):
            try:
                lines.append(self.logs.get_nowait())
            except queue.Empty:
                break
        if lines:
            self.append_log("".join(lines))
        while self.logs.empty() and not self.events.empty():
            event, owner, result = self.events.get_nowait()
            if event == "scan":
                self.scanning = False
                self.scanned_path = owner
                self.opencl, text = result
                self.hardware.set(text)
            elif event == "exit" and owner is self.process:
                self.process = None
                self.status.set("Stopped" if self.stopping else "Miner exited · code " + str(result))
                self.append_log("Miner exited (code " + str(result) + ").\n")
                self.stopping = False
            elif event == "stop_error" and owner is self.process:
                self.stopping = False
                self.status.set("Could not stop the miner; retry Stop or close it externally")
            self.refresh()
        if self.closing and self.process is None and not self.scanning:
            self.root.destroy()
            return
        self.root.after(100, self.poll)

    def close(self):
        self.closing = True
        self.stop()
        if self.scanning:
            self.status.set("Closing when the GPU scan finishes…")
        self.refresh()


def main():
    global tk, ttk, filedialog, messagebox
    try:
        import tkinter as tk
        from tkinter import ttk, filedialog, messagebox
    except ImportError:
        sys.exit("This GUI requires Python with Tk. Install your platform's Python/Tk package, then run it again.")
    if tk.TkVersion < 8.6:
        sys.exit("This GUI requires Tk 8.6 or newer. Install a current Python with Tk; Apple's system Tk 8.5 is unsupported.")
    root = tk.Tk()
    window = MinerWindow(root)
    try:
        root.mainloop()
    finally:
        if window.process is not None:
            stop_process(window.process)


if __name__ == "__main__":
    main()
