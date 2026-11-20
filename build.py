"""Build the ByteGuard pybind11 extension module."""

import os
import shutil
import subprocess
import sys
import sysconfig
import tempfile
from pathlib import Path

import pybind11

ROOT = Path(__file__).resolve().parent


def find_vcvars64() -> str:
    vswhere = (
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe"
    )
    if not vswhere.exists():
        raise RuntimeError("Could not find cl.exe or Visual Studio vswhere.exe")
    vs_dir = subprocess.check_output(
        [
            str(vswhere),
            "-latest",
            "-prerelease",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ],
        text=True,
    ).strip()
    vcvars = Path(vs_dir) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    if not vcvars.exists():
        raise RuntimeError(f"MSVC vcvars64.bat not found under {vs_dir}")
    return str(vcvars)


def main() -> None:
    ext_suffix = sysconfig.get_config_var("EXT_SUFFIX") or (".pyd" if sys.platform == "win32" else ".so")
    out_file = ROOT / "web" / f"byteguard_cpp{ext_suffix}"
    out_file.parent.mkdir(parents=True, exist_ok=True)
    py_inc = sysconfig.get_path("include")
    pybind_inc = pybind11.get_include()

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        if sys.platform == "win32":
            py_libs = Path(sys.base_prefix) / "libs"
            implib = tmp_dir / "byteguard_cpp.lib"
            cl_mod = (
                f'cl /nologo /O2 /EHsc /MD /std:c++17 /DNOMINMAX /LD '
                f'/I"{py_inc}" /I"{pybind_inc}" "{ROOT / "web" / "bindings.cpp"}" '
                f'/Fe:"{out_file}" /Fo:"{tmp_dir / "bindings.obj"}" '
                f'/link /LIBPATH:"{py_libs}" /IMPLIB:"{implib}"'
            )
            bat = tmp_dir / "build.bat"
            if shutil.which("cl") is None:
                bat.write_text(f'@echo off\r\ncall "{find_vcvars64()}" >nul || exit /b 1\r\n{cl_mod}\r\n', encoding="utf-8")
            else:
                bat.write_text(f'@echo off\r\n{cl_mod}\r\n', encoding="utf-8")
            subprocess.check_call(["cmd", "/c", str(bat)])
        else:
            cxx = os.environ.get("CXX", "c++" if sys.platform == "darwin" else "g++")
            cmd = [cxx, "-O2", "-std=c++17", "-shared", "-fPIC"]
            if sys.platform == "darwin":
                cmd += ["-undefined", "dynamic_lookup"]
            cmd += [f"-I{py_inc}", f"-I{pybind_inc}", str(ROOT / "web" / "bindings.cpp"), "-o", str(out_file)]
            subprocess.check_call(cmd)

    print(f"Built {out_file.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
