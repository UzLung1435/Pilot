"""Sony USB transport used by Pilot camera tools."""
import os, sys, time
from pathlib import Path
_dlls = Path(sys.executable).parent / "Lib" / "site-packages" / "libusb_package"
if _dlls.is_dir():
    os.environ["PATH"] = str(_dlls) + os.pathsep + os.environ.get("PATH", "")
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "vendor" / "pmca-re"))
import usb.util
from pmca.usb.sony import SONY_ID_VENDOR, SonySenserAuthDevice, SonySenserCamera, SonySenserDevice
from pmca.usb.driver.generic import GenericUsbDriver, MscBbbDriver
from pmca.usb.driver.generic import libusb as pmca_libusb
from pmca.usb import MscDevice
SVC_PID = 0x0336

def log(msg):
    try:
        print(msg, flush=True)
    except UnicodeEncodeError:
        print(msg.encode("gbk", "replace").decode("gbk"), flush=True)


def dev_key(dev):
    a = getattr(dev, "address", None)
    if a is None:
        inner = getattr(dev, "dev", None)
        a = getattr(inner, "address", None)
    return a if a is not None else hex(id(dev))


def open_libusb():
    return [pmca_libusb.MscContext(), pmca_libusb.VendorSpecificContext()]


def list_sony(ctxs, quiet=False):
    found = []
    for ctx in ctxs:
        try:
            for dev in ctx.listDevices(SONY_ID_VENDOR):
                try:
                    found.append((dev, ctx, ctx.openDevice(dev)))
                except Exception as e:
                    if not quiet:
                        log("     (open failed) %r" % (e,))
        except Exception as e:
            if not quiet:
                log("     (context failed) %r" % (e,))
    seen = set()
    uniq = []
    for dev, ctx, drv in found:
        k = dev_key(dev)
        if k not in seen:
            seen.add(k)
            uniq.append((dev, ctx, drv))
    return uniq


def try_auth(drv):
    a = SonySenserAuthDevice(drv)
    try:
        a.start()
    except Exception:
        pass
    a.authenticate()
    return a


def usb_reset_dev(drv):
    d = getattr(getattr(drv, "backend", None), "dev", None)
    if d is None:
        d = getattr(drv, "dev", None)
    if d is None:
        d = getattr(getattr(drv, "dev", None), "dev", None)
    if d is None or not hasattr(d, "reset"):
        raise RuntimeError("cannot find the pyusb device object")
    try:
        usb.util.dispose_resources(d)
    except Exception:
        pass
    d.reset()
    time.sleep(2.5)


def auth_with_recover(ctxs, drv):
    try:
        return try_auth(drv), drv
    except Exception as e:
        log("authentication failed (%r); USB reset and one retry ..." % (e,))
        try:
            usb_reset_dev(drv)
        except Exception as e2:
            log("reset failed: %r" % (e2,))
        time.sleep(1.0)
        for dev, ctx, drv2 in list_sony(ctxs, quiet=True):
            if drv2.getId()[1] == drv.getId()[1]:
                return try_auth(drv2), drv2
        raise RuntimeError("device did not come back after reset")


def set_timeout_20s(drv):
    for chain in (("backend", "dev"), ("dev",)):
        obj = drv
        for attr in chain:
            obj = getattr(obj, attr, None)
            if obj is None:
                break
        if obj is not None and hasattr(obj, "default_timeout"):
            obj.default_timeout = 20000


class Session:
    def __init__(self, skip_hasp=False):
        # readHasp() crosses into a private API.  Callers that must stay on the
        # documented Jiritsu channel pass skip_hasp=True and get an identical
        # session without that call.
        self.skip_hasp = skip_hasp

    def __enter__(self):
        self.healthy = True
        self.ctxs = open_libusb()
        devs = list_sony(self.ctxs)
        log("visible Sony devices: %d" % len(devs))
        for dev, ctx, drv in devs:
            log("  id = %s" % (drv.getId(),))

        svc = [(d, c, r) for (d, c, r) in devs if r.getId()[1] == SVC_PID]
        if svc:
            log("service identity already present -- authenticating directly")
            self.auth, self.drv = auth_with_recover(self.ctxs, svc[0][2])
        else:
            msc = None
            for d, c, drv in devs:
                if isinstance(drv, (MscBbbDriver, GenericUsbDriver)):
                    try:
                        info = MscDevice(drv).getDeviceInfo()
                        man = getattr(info, "manufacturer", "")
                        if isinstance(man, (bytes, bytearray)):
                            man = man.decode("utf-8", "ignore")
                        if "Sony" in str(man):
                            msc = drv
                            log("MSC device: pid=0x%04X model=%s"
                                % (drv.getId()[1], getattr(info, "model", "?")))
                            break
                    except Exception:
                        continue
            if msc is None:
                raise RuntimeError("no Sony MSC camera and no service identity; set the camera "
                                   "to the mass-storage style USB mode and replug the cable")
            log("asking the camera to raise the service identity ...")
            auth = SonySenserAuthDevice(msc)
            try:
                auth.start()
            except Exception as e:
                log("start warning: %r" % (e,))
            auth.authenticate()
            log("round 1 authentication passed")
            try:
                auth.stop()
            except Exception:
                pass
            senser = None
            t0 = time.time()
            while time.time() - t0 < 25:
                time.sleep(1.0)
                for d, c, drv in list_sony(self.ctxs, quiet=True):
                    if drv.getId()[1] == SVC_PID:
                        try:
                            self.auth = try_auth(drv)
                            self.drv = drv
                            senser = True
                            break
                        except Exception as e:
                            log("  0x0336 authentication failed %r, retrying" % (e,))
                if senser:
                    break
            if not senser:
                raise RuntimeError("service identity 0x0336 did not appear within 25 s "
                                   "(check the Zadig driver for 054C:0336)")
            log("round 2 authentication passed")

        set_timeout_20s(self.drv)
        self.channel = SonySenserDevice(self.auth.driver)
        self.camera = SonySenserCamera(self.channel)
        # dscan_run does this warm-up after auth; without it the first
        # Jiritsu command can stall on the service channel.
        if not self.skip_hasp:
            try:
                self.camera.readHasp()
            except Exception as e:
                log("readHasp warning: %r" % (e,))
        return self

    def __exit__(self, *args):
        if getattr(self, "healthy", False):
            try:
                self.auth.stop()
            except Exception:
                pass

    def call(self, fn, *args):
        if not self.healthy:
            raise RuntimeError("session state is unknown; stopping further commands")
        try:
            return fn(*args)
        except Exception:
            self.healthy = False
            raise

    def jiritsu(self, cmd, data=b"", missing=False):
        if cmd not in (15, 16, 17, 18):
            raise ValueError("jiritsu command not allowed")
        response, out = self.call(self.channel.sendSenserPacket, 0x40,
                                  bytes.fromhex("0110") + bytes([cmd, 0]) + data)
        if response or len(out) < 4:
            raise RuntimeError("malformed Jiritsu response %r" % (response,))
        status = int.from_bytes(out[0:4], "little")
        if missing and status == 0x10000006:
            return None
        if status:
            raise RuntimeError("Jiritsu status 0x%x" % status)
        return out[4:]

    def select(self, path):
        if not path.startswith("/tmp/jiritsu/") or "\0" in path or len(path) > 510:
            raise ValueError("path not allowed")
        self.jiritsu(15, b"\0" + path.encode("ascii") + b"\0")

    def read(self, name, missing=False):
        self.select("/tmp/jiritsu/" + name)
        return self.jiritsu(17, b"\0\xff\0", missing)

    def read_path(self, path, missing=False):
        self.select(path)
        return self.jiritsu(17, b"\0\xff\0", missing)
