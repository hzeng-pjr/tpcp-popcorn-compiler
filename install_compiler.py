#!/usr/bin/env python3

from __future__ import print_function

import argparse
import multiprocessing as mp
import os, os.path
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import re
import pathlib
import glob

#================================================
# GLOBALS
#================================================

# The machine on which we're compiling
host = platform.machine()

# Supported targets
supported_targets = ['aarch64', 'x86_64']
# LLVM names for targets
llvm_targets = {
    'aarch64' : 'AArch64',
    'powerpc64' : 'PowerPC',
    'powerpc64le' : 'PowerPC',
    'x86_64' : 'X86'
}

linux_targets = {
    'aarch64' : 'arm64',
    'x86_64' : 'x86'
}

# The installation directory for the cross compilers
cross_dir = shutil.which("x86_64-linux-gnu-gcc")
if (cross_dir is None):
    print("Error: missing x86_64-linux-gnu-gcc toolchain")
    sys.exit(1);

cross_path = os.path.dirname(os.path.dirname(cross_dir))

# LLVM URL
llvm_url = 'https://github.com/llvm/llvm-project.git'
#llvm_url = 'file:///scratch/mirrors/llvm-project.git'
llvm_version = 9

# Binutils 2.32 URL
binutils_version = "binutils-2.34"
binutils_url = 'http://ftp.gnu.org/gnu/binutils/' + binutils_version + '.tar.bz2'
#binutils_url = 'file:///scratch/mirrors/' + binutils_version + '.tar.bz2'
binutils_dev = "/scratch/pjr/binutils-gdb"
local_binutils = False

# GNU libc (glibc)

# NOTE 1: The glibc version must match the version installed
# on the host operating system in order to allow the Popcorn
# application to use 3rd party libraries linked against the glibc
# provided by the system.

# NOTE 2: There's nothing prevention each node from using different
# versions of glibc. Also, the alignment tool is capable of allowing
# one node to glibc and another to use musl libc.

glibc_version = "2.31" # Ubunut 20.04
glibc_url = "git://sourceware.org/git/glibc.git"
#glibc_url = "file:///scratch/mirrors/glibc.git"
glibc_dev = "/scratch/pjr/glibc"
local_glibc = False

gcc_version = "9.3.0"
gcc_url = "git://gcc.gnu.org/git/gcc.git"
#gcc_url = "file:///scratch/mirrors/gcc.git"

linux_version = "v5.4.169"
linux_url = "git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git"
#linux_url = "file:///scratch/mirrors/linux-stable.git"

xorg_url = "https://gitlab.freedesktop.org/xorg/lib/"
libmd_url = "https://git.hadrons.org/git/libmd.git"
libbsd_url = "https://gitlab.freedesktop.org/libbsd/libbsd.git"
libuuid_url = "git://git.kernel.org/pub/scm/utils/util-linux/util-linux.git"

parallelize_gnu_build = True

#================================================
# ARGUMENT PARSING
#================================================
def setup_argument_parsing():
    global supported_targets

    parser = argparse.ArgumentParser(
                formatter_class=argparse.ArgumentDefaultsHelpFormatter)

    config_opts = parser.add_argument_group('Configuration Options')
    config_opts.add_argument("--base-path",
                        help="Base path of popcorn-compiler repo",
                        default=os.getcwd(),
                        dest="base_path")
    config_opts.add_argument("--install-path",
                        help="Install path of Popcorn compiler",
                        default="/usr/local/popcorn",
                        dest="install_path")
    config_opts.add_argument("--threads",
                        help="Number of threads to build compiler with",
                        type=int,
                        default=mp.cpu_count(),
                        dest="threads")

    process_opts = parser.add_argument_group('Component Installation Options')
    process_opts.add_argument("--skip-prereq-check",
                        help="Skip checking for prerequisites (see README)",
                        action="store_true",
                        dest="skip_prereq_check")
    process_opts.add_argument("--install-all",
                        help="Install all toolchain components",
                        action="store_true",
                        dest="install_all")

    process_opts.add_argument("--install-llvm-clang",
                        help="Install LLVM and Clang",
                        action="store_true",
                        dest="llvm_clang_install")

    process_opts.add_argument("--install-binutils",
                        help="Install binutils",
                        action="store_true",
                        dest="binutils_install")

    process_opts.add_argument("--install-gcc-glibc",
                        help="Install GCC and GLIBC",
                        action="store_true",
                        dest="gcc_glibc_install")

    process_opts.add_argument("--install-musl",
                        help="Install musl-libc",
                        action="store_true",
                        dest="musl_install")
    process_opts.add_argument("--install-libelf",
                        help="Install libelf",
                        action="store_true",
                        dest="libelf_install")
    process_opts.add_argument("--install-libopenpop",
                        help="Install libopenpop, Popcorn's OpenMP runtime",
                        action="store_true",
                        dest="libopenpop_install")
    process_opts.add_argument("--install-stack-transform",
                        help="Install stack transformation library",
                        action="store_true",
                        dest="stacktransform_install")
    process_opts.add_argument("--install-migration",
                        help="Install migration library",
                        action="store_true",
                        dest="migration_install")
    process_opts.add_argument("--install-remote-io",
                        help="Install remote I/O library",
                        action="store_true",
                        dest="remote_io_install")
    process_opts.add_argument("--install-stack-depth",
                        help="Install application call information library",
                        action="store_true",
                        dest="stackdepth_install")
    process_opts.add_argument("--install-x11",
                        help="Install X11 Libraries",
                        action="store_true",
                        dest="x11_install")

    process_opts.add_argument("--install-tools",
                        help="Install compiler tools",
                        action="store_true",
                        dest="tools_install")

    process_opts.add_argument("--install-utils",
                        help="Install utility scripts",
                        action="store_true",
                        dest="utils_install")

    process_opts.add_argument("--install-namespace",
                        help="Install namespace tools (deprecated)",
                        action="store_true",
                        dest="namespace_install")

    selectable_targets = list(supported_targets)
    selectable_targets.extend(["all"])
    build_opts = parser.add_argument_group('Build options (per-step)')
    build_opts.add_argument("--targets",
                        help="Comma-separated list of targets to build " \
                             "(options: {})".format(selectable_targets),
                        default="all",
                        dest="targets")
    build_opts.add_argument("--debug-stack-transformation",
                        help="Enable debug output for stack transformation library",
                        action="store_true",
                        dest="debug_stack_transformation")
    build_opts.add_argument("--libmigration-type",
                        help="Choose configuration for libmigration " + \
                             "(see INSTALL for more details)",
                        choices=['env_select', 'native', 'debug'],
                        dest="libmigration_type")
    build_opts.add_argument("--enable-libmigration-timing",
                        help="Turn on timing in migration library",
                        action="store_true",
                        dest="enable_libmigration_timing")
    build_opts.add_argument("--debug-remote-io",
                        help="Enable debug output for remote I/O library",
                        action="store_true",
                        dest="debug_remote_io")

    process_opts.add_argument("--with-llvm-3_7",
                        help="Use LLVM-3.7 instead of LLVM-9",
                        action="store_true",
                        dest="use_llvm37")

    process_opts.add_argument("--install-glibc",
                        help="Install GLIBC",
                        action="store_true",
                        dest="glibc_install")

    process_opts.add_argument("--clean",
                        help="Remove any build directories",
                        action="store_true",
                        dest="clean_install")

    return parser

def postprocess_args(args):
    global supported_targets
    global llvm_targets
    global llvm_version

    # Clean up paths
    args.base_path = os.path.abspath(args.base_path)
    args.install_path = os.path.abspath(args.install_path)

    # Sanity check targets requested & generate LLVM-equivalent names
    user_targets = args.targets.split(',')
    args.install_targets = []
    for target in user_targets:
        if target == "all":
            args.install_targets = supported_targets
            break
        else:
            if target not in supported_targets:
                print("Unsupported target '{}'!".format(target))
                sys.exit(1)
            args.install_targets.append(target)

    args.llvm_targets = ""
    for target in args.install_targets:
        args.llvm_targets += llvm_targets[target] + ";"
    args.llvm_targets = args.llvm_targets[:-1]

    # Implicitly build binutils with gcc-glibc
    if args.gcc_glibc_install:
        args.binutils_install = True

    if args.use_llvm37:
        llvm_version = 3.7

    # Turn on all components for installation if requested
    if args.install_all:
        args.llvm_clang_install = True
        args.binutils_install = True
        args.gcc_glibc_install = True
        args.musl_install = False
        args.libelf_install = True
        args.libopenpop_install = False
        args.stacktransform_install = True
        args.migration_install = True
        args.remote_io_install = True
        args.stackdepth_install = True
        args.x11_install = True
        args.tools_install = True
        args.utils_install = True

    # Add install_path to the PATH environment variable
    os.environ["PATH"] = args.install_path + "/bin:" + os.environ["PATH"]

def warn_stupid(args):
    if len(args.install_targets) < 2:
        print("WARNING: installing Popcorn compiler with < 2 architectures!")
    if platform.machine() != 'x86_64':
        print("WARNING: installing Popcorn compiler on '{}', " \
              "you may get errors!".format(platform.machine()))

#================================================
# PREREQUISITE CHECKING
#   Determines if all needed prerequisites are installed
#   See popcorn-compiler/README for more details
#================================================
def _check_for_prerequisite(prereq):
    try:
        out = subprocess.check_output([prereq, '--version'],
                                      stderr=subprocess.STDOUT).decode("utf-8")

    except Exception:
        print('{} not found!'.format(prereq))
        return None
    else:
        out = out.split('\n')[0]
        return out

def check_for_prerequisites(args):
    success = True

    print('Checking for prerequisites (see README for more info)...')
    gcc_prerequisites = ['x86_64-linux-gnu-g++']
    for target in args.install_targets:
        gcc_prerequisites.append('{}-linux-gnu-gcc'.format(target))
    other_prerequisites = ['flex', 'bison', 'cmake', 'libtool', 'make', 'zip']

    for prereq in gcc_prerequisites:
        out = _check_for_prerequisite(prereq)
        if out:
            # GCC version string format:
            # $CC (name of compiler) x.y.z.
            #
            # split() won't work on Ubuntu compilers because of the central
            # (Ubuntu <version>) gets concatenated into separate strings.
            gcc_version = re.findall("\) (\d+\.\d+\.\d+)", out)

            major, minor, micro = [int(v) for v in gcc_version[0].split('.')]
            version = major * 10 + minor
            if not (version >= 48):
                print('{} 4.8 or higher required to continue'.format(prereq))
                success = False
        else:
            success = False

    for prereq in other_prerequisites:
        out = _check_for_prerequisite(prereq)
        if not out:
            success = False

    return success

#================================================
# BUILD API
#================================================

def run_cmd(name, cmd, ins=None, outs=None, use_shell=False):
    try:
        rv = subprocess.check_call(cmd, stdin=ins, stdout=outs,
                                   stderr=subprocess.STDOUT, shell=use_shell)
    except subprocess.CalledProcessError as e:
        print('Could not {} ({})!'.format(name, e))
        sys.exit(e.returncode)

def get_cmd_output(name, cmd, ins=None, use_shell=False):
    try:
        out = subprocess.check_output(cmd, stdin=ins, stderr=subprocess.STDOUT,
                                      shell=use_shell)
    except subprocess.CalledProcessError as e:
        print('Could not {} ({})!'.format(name, e))
        sys.exit(e.returncode)
    return out.decode('utf-8')

def do_copy_files(src, dst):
    for file in glob.glob(src):
        print("copying {} to {}".format(file, dst))
        shutil.copy(file, dst)

def do_copy_file_type(src, dst, type):
    for file in pathlib.Path(src).rglob(type):
        print("copying {} to {}".format(file, dst))
        shutil.copy(file, dst)

def do_git_clone(url, branch, dest_dir, name):
    print("Downloading {}...".format(name))

    if os.path.exists(dest_dir):
        os.chdir(dest_dir)
        args = ['git', 'clean', '-dfx']
        run_cmd('reseting {} source'.format(name), args)
        args = ['git', 'reset', '--hard']
        run_cmd('cleaning {} source'.format(name), args)
    else:
        args = ['git', 'clone', "--depth", "1", "-b", branch, url, dest_dir]
        run_cmd('download {} source'.format(name), args)

def install_clang_llvm(base_path, install_path, num_threads, llvm_targets):

    llvm_download_path = os.path.join(install_path, 'src', 'llvm')
    clang_download_path = os.path.join(llvm_download_path, 'clang')

    patch_base = os.path.join(base_path, 'patches', 'llvm')
    llvm_patch_path = os.path.join(patch_base,
                                   'llvm-{}.patch'.format(llvm_version))

    if llvm_version == 3.7:
        cmake_flags = ['-DCMAKE_INSTALL_PREFIX={}'.format(install_path),
                       '-DLLVM_TARGETS_TO_BUILD={}'.format(llvm_targets),
                       '-DCMAKE_BUILD_TYPE=Debug',
                       '-DLLVM_ENABLE_RTTI=ON',
                       '-DBUILD_SHARED_LIBS=ON']
    else:
        cmake_flags = ['-DCMAKE_INSTALL_PREFIX={}'.format(install_path),
                       '-DLLVM_TARGETS_TO_BUILD={}'.format(llvm_targets),
                       '-DCMAKE_BUILD_TYPE=Debug',
                       '-DLLVM_ENABLE_RTTI=ON',
                       '-DBUILD_SHARED_LIBS=ON',
                       '-DLLVM_EXTERNAL_PROJECTS="clang;"',
                       '-DLLVM_EXTERNAL_CLANG_SOURCE_DIR={}'
                       .format(llvm_download_path + "/clang")]

    #=====================================================
    # DOWNLOAD LLVM
    #=====================================================
    do_git_clone(llvm_url, "release/{}.x".format(llvm_version),
                 llvm_download_path, "llvm")

    #=====================================================
    # PATCH LLVM
    #=====================================================
    with open(llvm_patch_path, 'r') as patch_file:
        print('Patching LLVM...')
        args = ['patch', '-p1', '-d', llvm_download_path]
        run_cmd('patch LLVM', args, patch_file)

    # LLVM-3.7's build system needs clang inside llvm/tools
    if llvm_version == 3.7:
        clang_src = os.path.join(llvm_download_path, 'clang')
        clang_dst = os.path.join(llvm_download_path, 'llvm', 'tools')
        shutil.move(clang_src, clang_dst)

    #=====================================================
    # BUILD AND INSTALL LLVM
    #=====================================================
    cur_dir = os.getcwd()
    os.chdir(llvm_download_path + '/llvm')
    os.mkdir('build')
    os.chdir('build')

    print('Running CMake...')
    args = ['cmake'] + cmake_flags + ['..']
    run_cmd('run CMake', args)

    if llvm_version > 3.7:
        print('Installing LLVM headers...')
        args = ['make', '-j', str(num_threads), 'install-llvm-headers']
        run_cmd('run Make headers', args)

    print('Running Make...')
    if llvm_version > 3.7:
        args = ['make', '-j', str(num_threads), 'install-llvm-headers']
        run_cmd('run Make headers', args)

    args = ['make', '-j', str(num_threads)]
    run_cmd('run Make', args)
    args += ['install']
    run_cmd('install clang/LLVM', args)

    os.chdir(cur_dir)

def install_binutils(base_path, install_path, num_threads, targets):

    binutils_install_path = os.path.join(install_path, 'src', binutils_version)
    patch_path = os.path.join(base_path, 'patches', 'binutils-gold',
                              '{}.patch'.format(binutils_version))

    #=====================================================
    # DOWNLOAD BINUTILS
    #=====================================================
    print('Downloading binutils source...')

    if local_binutils:
        args = ['rm', '-rf', binutils_install_path]
        run_cmd('cleanup binutils sources', args)

        args = ['cp', '-a', binutils_dev, binutils_install_path]
        run_cmd('download binutils source', args)
    else:
        binutils_tarball = '{}.tar.bz2'.format(binutils_version)
        try:
            urllib.request.urlretrieve(binutils_url, binutils_tarball)
            with tarfile.open(binutils_tarball, 'r:bz2') as f:
                f.extractall(path=os.path.join(install_path, 'src'))
        except Exception as e:
            print('Could not download/extract binutils source ({})!'.format(e))
            sys.exit(1)

        #=====================================================
        # PATCH BINUTILS
        #=====================================================
        print("Patching binutils...")
        with open(patch_path, 'r') as patch_file:
            args = ['patch', '-p1', '-d', binutils_install_path]
            run_cmd('patch binutils', args, patch_file)

    #=====================================================
    # BUILD AND INSTALL BINUTILS
    #=====================================================
    cur_dir = os.getcwd()

    for target in targets:
        target_install_path = os.path.join(install_path, target)

        configure_flags = ['--prefix={}'.format(install_path),
                           '--target={}'.format(target + '-popcorn-linux-gnu'),
                           '--enable-gold',
                           '--enable-ld',
                           '--disable-gdb',
                           '--disable-libquadmath',
                           '--disable-libquadmath-support',
                           '--disable-libstdcxx',
                           '--disable-werror',
                           '--with-sysroot={}'.format(target_install_path)]

        os.chdir(binutils_install_path)
        shutil.rmtree(binutils_install_path
                      + "/build.{}".format(target), ignore_errors=True)
        os.mkdir('build.{}'.format(target))
        os.chdir('build.{}'.format(target))

        print("Configuring binutils...")
        args = ['../configure'] + configure_flags \
            + ["--target={}-popcorn-linux-gnu".format(target)];
        run_cmd('configure binutils', args)

        print('Making binutils...')
        args = ['make', '-j', str(num_threads)]
        run_cmd('run Make', args)
        args += ['install']
        run_cmd('install binutils', args)

        ld_gold = install_path + "/bin/ld.gold"

        if os.path.exists (ld_gold):
            os.unlink (ld_gold)

            os.symlink (install_path + "/bin/aarch64-popcorn-linux-gnu-ld.gold",
                        ld_gold)

    os.chdir(cur_dir)

def install_musl(base_path, install_path, target, num_threads):
    cur_dir = os.getcwd()

    #=====================================================
    # CONFIGURE & INSTALL MUSL
    #=====================================================
    target_install_path = os.path.join(install_path, target)
    os.chdir(os.path.join(base_path, 'lib', 'musl-1.1.18'))

    if os.path.isfile('Makefile'):
        run_cmd('clean musl', ['make', 'distclean'])

    kernel_arg = "POPCORN_5_2"

    print("Configuring musl ({})...".format(target))
    args = ' '.join(['./configure',
                     '--prefix={}'.format(target_install_path),
                     '--target={}-linux-gnu'.format(target),
                     '--enable-optimize',
                     '--enable-debug',
                     '--enable-warnings',
                     '--enable-wrapper=all',
                     '--disable-shared',
                     'CC={}/bin/clang'.format(install_path),
                     'CFLAGS="-target {}-linux-gnu -popcorn-libc"' \
                     .format(target),
                     'KERVER="{}"'.format(kernel_arg)])
    print(args)
    run_cmd('configure musl-libc ({})'.format(target), args, use_shell=True)

    print('Making musl ({})...'.format(target))
    args = ['make', '-j', str(num_threads)]
    run_cmd('make musl-libc ({})'.format(target), args)
    args += ['install']
    run_cmd('install musl-libc ({})'.format(target), args)

    os.chdir(cur_dir)

def install_gcc_glibc(base_path, install_path, install_targets, num_threads):
    cur_dir = os.getcwd()

    gcc_download_path = os.path.join(install_path, 'src', 'gcc')
    glibc_download_path = os.path.join(install_path, 'src', 'glibc')
    linux_download_path = os.path.join(install_path, 'src', 'linux-kernel')

    #TODO: Check whether 'install_path'/src exists.

    args = ['rm', '-rf', gcc_download_path, glibc_download_path,
            linux_download_path]
    run_cmd('cleanup gcc and glibc sources', args)

    args = ['git', 'clone', '--depth', '1', '-b',
            'releases/gcc-' + gcc_version, gcc_url, gcc_download_path]
    run_cmd('download GCC source', args)

    # Download GCC prerequisites
    os.chdir(gcc_download_path)
    args = [ 'contrib/download_prerequisites' ]
    run_cmd('Download GCC prerequisites', args)
    os.chdir(cur_dir)

    if local_glibc:
        # Just build glibc locally
        glibc_download_path = glibc_dev
    else:
        args = ['git', 'clone', '--depth', '1', '-b',
                "release/" + glibc_version + "/master",
                glibc_url, glibc_download_path]
        run_cmd('download glibc source', args)

        # Patch GLIBC
        glibc_patch_path = os.path.join(base_path, 'patches', 'glibc',
                                        'glibc-{}.patch'.format(glibc_version))
        with open(glibc_patch_path, 'r') as patch_file:
            print('Patching GLIBC...')
            args = ['patch', '-p1', '-d', glibc_download_path]
            run_cmd('patch GLIBC', args, patch_file)

    args = ['git', 'clone', '--depth', '1', '-b', linux_version, linux_url,
            linux_download_path]
    run_cmd('download Linux Kernel source', args)

    for target in install_targets:
        sysroot = os.path.join(install_path, target)
        sysroot_inc = os.path.join(sysroot, 'include')
        libdir_path = os.path.join(install_path, sysroot, 'lib')
        llvm_target_path = os.path.join(install_path, target)

        # Prepare the sysroot
        if not os.path.exists(sysroot):
            os.makedirs(sysroot, exist_ok=True)

        os.chdir(linux_download_path)
        args = ['make', 'ARCH={}'.format(linux_targets[target]),
                'INSTALL_HDR_PATH="{}"'.format(sysroot),
                'headers_install']
        run_cmd('Install Linux headers', args)

        # Certain applications, such as H-container redis, expect
        # $INST/target/include to be populated with the Linux headers
        args = ['make', 'ARCH={}'.format(linux_targets[target]),
                'INSTALL_HDR_PATH="{}"'.format(llvm_target_path),
                'headers_install']
        run_cmd('Install Linux headers', args)

        # GCC Stage 1
        gcc_stage_1_dir = "build-gcc-stage-1." + target
        os.chdir(gcc_download_path)
        args = ['rm', '-rf', gcc_stage_1_dir]
        run_cmd('clean ' + gcc_stage_1_dir, args)
        os.mkdir(gcc_stage_1_dir)
        os.chdir(gcc_stage_1_dir)

        args = [gcc_download_path + '/configure',
                '--prefix={}'.format(install_path),
                '--target={}-popcorn-linux-gnu'.format(target),
                '--with-sysroot={}'.format(sysroot),
                '--enable-languages=c',
                '--with-newlib',
                '--without-headers',
                '--disable-shared',
                '--disable-threads',
                '--enable-__cxa_atexit',
                '--enable-languages=c',
                '--disable-libatomic',
                '--disable-libmudflap',
                '--disable-libssp',
                '--disable-libquadmath',
                '--disable-libgomp',
                '--disable-nls',
                '--disable-bootstrap',
                #'--disable-multilib',
                '--libdir={}/lib'.format(sysroot),
                "CFLAGS_FOR_TARGET='-ffunction-sections -fdata-sections'"]
        run_cmd('Configure GCC Stage 1 for ' + target, args)

        args = ['make', '-j{}'.format(num_threads), 'inhibit-libc=true',
                'all-gcc']
        run_cmd('Build GCC Stage 1 for ' + target, args)

        args = ['make', '-j{}'.format(num_threads), 'inhibit-libc=true',
                'install-gcc']
        run_cmd('Install GCC Stage 1 for ' + target, args)

        # glibc Stage 1
        glibc_stage_1_dir = os.path.join(glibc_download_path,
                                         "build-glibc-stage-1." + target)
        os.chdir(glibc_download_path)
        args = ['rm', '-rf', glibc_stage_1_dir]
        run_cmd('clean ' + glibc_stage_1_dir, args)
        os.mkdir(glibc_stage_1_dir)
        os.chdir(glibc_stage_1_dir)

        args = [glibc_download_path + '/configure',
                '--prefix=/',
                '--exec-prefix=/',
                '--oldincludedir=/include',
                '--target={}-popcorn-linux-gnu'.format(target),
                '--host={}-popcorn-linux-gnu'.format(target),
                '--enable-shared',
                '--with-headers={}/include'.format(sysroot),
                '--disable-multilib',
                '--disable-werror',
                '--enable-kernel=4.4',
                '--with-__thread',
                '--with-tls',
                '--enable-addons=no',
                '--without-cvs',
                '--disable-profile',
                '--without-gd',
                'CFLAGS={}'.format("-Og -g3"),
                '--with-nonshared-cflags={}'.format("-ffunction-sections -fdata-sections")]
        run_cmd('Configure glibc Stage 1 for ' + target, args)

        args = ['make', 'install-bootstrap-headers=yes', 'install-headers',
                'install_root={}'.format(sysroot)]
        run_cmd('Build glibc Stage 1 for ' + target, args)

        stubs_h = os.path.join(sysroot, 'include', 'gnu', 'stubs.h')
        pathlib.Path(stubs_h).touch()

        src = os.path.join(glibc_download_path, "include", "features.h")
        dst = os.path.join(sysroot,'include', "features.h")
        shutil.copyfile(src, dst)

        src = os.path.join(glibc_stage_1_dir, 'bits', 'stdio_lim.h')
        dst = os.path.join(sysroot, 'include', 'bits', 'stdio_lim.h')
        shutil.copyfile(src, dst)

        args = ['make', 'csu/subdir_lib']
        run_cmd('make csu/subdir_lib for ' + target, args)

        if not os.path.exists(sysroot + "/lib"):
            os.makedirs(sysroot + "/lib", exist_ok=True)

        src = os.path.join(glibc_stage_1_dir, 'csu')
        dst = os.path.join(sysroot, 'lib')
        for i in ['crt1.o', 'crti.o', 'crtn.o']:
            print ("cp {} {}".format(src + "/" + i, dst + "/" + i))
            shutil.copyfile(os.path.join(src, i), os.path.join(dst, i))

        args = ['{}-linux-gnu-gcc'.format(target),
                '-o', '{}/lib/libc.so'.format(sysroot),
                '-nostdlib', '-nostartfiles', '-shared', '-x', 'c',
                '/dev/null']
        run_cmd('Build dummy libc.so for ' + target, args)

        # GCC Stage 2
        gcc_stage_2_dir = "build-gcc-stage-2." + target
        os.chdir(gcc_download_path)
        args = ['rm', '-rf', gcc_stage_2_dir]
        run_cmd('clean ' + gcc_stage_2_dir, args)
        os.mkdir(gcc_stage_2_dir)
        os.chdir(gcc_stage_2_dir)

        args = [gcc_download_path + '/configure',
                '--prefix={}'.format(install_path),
                '--with-native-system-header-dir=/include',
                '--oldincludedir=/include',
                '--target={}-popcorn-linux-gnu'.format(target),
                '--with-sysroot={}'.format(sysroot),
                '--with-local-prefix={}'.format(sysroot),
                '--enable-languages=c',
                '--enable-shared',
                '--enable-__cxa_atexit',
                '--disable-initfini-array',
                '--disable-libgomp',
                '--disable-libmudflap',
                '--disable-libmpx',
                '--disable-libssp',
                '--disable-libquadmath',
                '--disable-libquadmath-support',
                "--with-host-libstdcxx='-static-libgcc -Wl,-Bstatic,-lstdc++,-Bdynamic -lm'",
                '--with-glibc-version={}'.format(glibc_version),
                '--disable-nls',
                '--disable-multilib',
                '--disable-bootstrap',
                #'--libdir={}/lib'.format(sysroot),
                'CFLAGS_FOR_TARGET={}'.format("-ffunction-sections -fdata-sections")]
        run_cmd('Configure GCC Stage 2 for ' + target, args)

        args = ['make', '-j{}'.format(num_threads), 'configure-gcc',
                'configure-libcpp', 'configure-build-libiberty']
        run_cmd('Build GCC Stage 2 for ' + target, args)

        args = ['make', '-j{}'.format(num_threads),
                'all-libcpp', 'all-build-libiberty']
        run_cmd('Build GCC Stage 2 for ' + target, args)

        args = ['make', '-j{}'.format(num_threads), 'configure-libdecnumber']
        run_cmd('Build GCC Stage 2 for ' + target, args)

        args = ['make', '-j{}'.format(num_threads),
                '-C', 'libdecnumber', 'libdecnumber.a']
        run_cmd('Build GCC Stage 2 for ' + target, args)

        args = ['make', '-j{}'.format(num_threads), 'configure-libbacktrace']
        run_cmd('Build GCC Stage 2 for ' + target, args)

        args = ['make', '-j{}'.format(num_threads),
                '-C', 'libbacktrace']
        run_cmd('Build GCC Stage 2 for ' + target, args)

        args = ['make', '-j{}'.format(num_threads),
                '-C', 'gcc', 'libgcc.mvars']
        run_cmd('Build GCC Stage 2 for ' + target, args)

        args = ['make', '-j{}'.format(num_threads),
                'all-gcc', 'all-target-libgcc']
        run_cmd('Build GCC Stage 2 for ' + target, args)

        args = ['make', '-j{}'.format(num_threads),
                'install-gcc', 'install-target-libgcc']
        run_cmd('Build GCC Stage 2 for ' + target, args)

        # glibc Stage 2 (final)
        glibc_stage_2_dir = os.path.join(glibc_download_path,
                                         "build-glibc-stage-2." + target)
        os.chdir(glibc_download_path)
        args = ['rm', '-rf', glibc_stage_2_dir]
        run_cmd('clean ' + glibc_stage_2_dir, args)
        os.mkdir(glibc_stage_2_dir)
        os.chdir(glibc_stage_2_dir)

        for i in ['libc.so', 'crt1.o', 'crti.o', 'crtn.o']:
            f = os.path.join(sysroot, 'lib', i)
            if os.path.exists(f):
                os.remove(f)

        args = [glibc_download_path + '/configure',
                '--prefix=/',
                '--exec-prefix=/',
                '--oldincludedir=/include',
                '--with-libdir={}'.format(libdir_path),
                '--target={}-popcorn-linux-gnu'.format(target),
                '--host={}-popcorn-linux-gnu'.format(target),
                'CXX={}-fake-g++'.format(target),
                '--disable-werror',
                '--enable-shared',
                '--enable-obsolete-rpc',
                '--with-headers={}/include'.format(sysroot),
                '--disable-multilib',
                '--enable-kernel=4.4',
                '--with-__thread',
                '--with-tls',
                '--without-cvs',
                '--enable-addons=no',
                '--disable-profile',
                '--without-gd',
                '--without-selinux',
                'libc_cv_slibdir=/lib',
                'libc_cv_rtlddir=/lib',
                'CFLAGS={}'.format("-Og -g3"),
                '--with-nonshared-cflags={}'.format("-ffunction-sections -fdata-sections")]
        run_cmd('Configure glibc Stage 2 for ' + target, args)

        args = ['make', '-j{}'.format(num_threads)]
        run_cmd('Build glibc Stage 2 for ' + target, args)

        args = ['make', '-j{}'.format(num_threads), 'install',
                'install_root={}'.format(sysroot)]
        run_cmd('Build glibc Stage 2 for ' + target, args)

        # GCC Stage 3 (final)
        gcc_stage_3_dir = "build-gcc-stage-3." + target
        os.chdir(gcc_download_path)
        args = ['rm', '-rf', gcc_stage_3_dir]
        run_cmd('clean ' + gcc_stage_3_dir, args)
        os.mkdir(gcc_stage_3_dir)
        os.chdir(gcc_stage_3_dir)

        args = [gcc_download_path + '/configure',
                '--with-native-system-header-dir=/include',
                '--oldincludedir=/include',
                '--target={}-popcorn-linux-gnu'.format(target),
                '--with-sysroot={}'.format(sysroot),
                '--with-local-prefix={}'.format(sysroot),
                '--enable-languages={}'.format("c,c++"),
                '--prefix={}'.format(install_path),
                'CFLAGS_FOR_TARGET={}'.format("-ffunction-sections -fdata-sections"),
                '--enable-__cxa_atexit',
                '--disable-initfini-array',
                '--enable-shared',
                '--enable-threads=posix',
                '--disable-libmudflap',
                '--disable-libssp',
                '--disable-libquadmath',
                '--disable-libsanitizer',
                '--disable-nls',
                '--disable-multilib',
                '--disable-bootstrap']
                #'--libdir={}'.format(libdir_path) ]

        run_cmd('Configure GCC Stage 3 for ' + target, args)

        args = ['make', '-j{}'.format(num_threads), 'all']
        run_cmd('Build GCC Stage 3 for ' + target, args)

        args = ['make', '-j{}'.format(num_threads), 'install-strip']
        run_cmd('Build GCC Stage 3 for ' + target, args)

        os.chdir(cur_dir)

def do_install_glibc(target, base_path, install_path, glibc_download_path,
                     num_threads):
    sysroot = os.path.join(install_path, target)
    libdir_path = os.path.join(install_path, sysroot, 'lib')
    llvm_target_path = os.path.join(install_path, target)
    linux_download_path = os.path.join(install_path, 'src', 'linux-kernel')
    cur_dir = os.getcwd()

    # Prepare the sysroot
    if not os.path.exists(sysroot):
        os.makedirs(sysroot, exist_ok=True)

    os.chdir(linux_download_path)
    args = ['make', 'ARCH={}'.format(linux_targets[target]),
            'INSTALL_HDR_PATH="{}"'.format(sysroot),
            'headers_install']
    run_cmd('Install Linux headers', args)

    # Certain applications, such as H-container redis, expect
    # $INST/target/include to be populated with the Linux headers
    args = ['make', 'ARCH={}'.format(linux_targets[target]),
            'INSTALL_HDR_PATH="{}"'.format(llvm_target_path),
            'headers_install']
    run_cmd('Install Linux headers', args)

    # glibc Stage 1
    glibc_stage_1_dir = os.path.join(glibc_download_path,
                                     "build-glibc-stage-1." + target)
    os.chdir(glibc_download_path)
    args = ['rm', '-rf', glibc_stage_1_dir]
    run_cmd('clean ' + glibc_stage_1_dir, args)
    os.mkdir(glibc_stage_1_dir)
    os.chdir(glibc_stage_1_dir)

    args = [glibc_download_path + '/configure',
            '--prefix=/',
            '--exec-prefix=/',
            '--oldincludedir=/include',
            '--target={}-popcorn-linux-gnu'.format(target),
            '--host={}-popcorn-linux-gnu'.format(target),
            '--enable-shared',
            '--with-headers={}/include'.format(sysroot),
            '--disable-multilib',
            '--disable-werror',
            '--enable-kernel=4.4',
            '--with-__thread',
            '--with-tls',
            '--enable-addons=no',
            '--without-cvs',
            '--disable-profile',
            '--without-gd',
            'CFLAGS={}'.format("-Og -g3"),
            '--with-nonshared-cflags={}'.format("-ffunction-sections -fdata-sections")]
    run_cmd('Configure glibc Stage 1 for ' + target, args)

    args = ['make', 'install-bootstrap-headers=yes', 'install-headers',
            'install_root={}'.format(sysroot)]
    run_cmd('Build glibc Stage 1 for ' + target, args)

    stubs_h = os.path.join(sysroot, 'include', 'gnu', 'stubs.h')
    pathlib.Path(stubs_h).touch()

    src = os.path.join(glibc_download_path, "include", "features.h")
    dst = os.path.join(sysroot, 'include', "features.h")
    shutil.copyfile(src, dst)

    src = os.path.join(glibc_stage_1_dir, 'bits', 'stdio_lim.h')
    dst = os.path.join(sysroot, 'include', 'bits', 'stdio_lim.h')
    shutil.copyfile(src, dst)

    args = ['make', 'csu/subdir_lib']
    run_cmd('make csu/subdir_lib for ' + target, args)

    src = os.path.join(glibc_stage_1_dir, 'csu')
    dst = os.path.join(sysroot, 'lib')
    for i in ['crt1.o', 'crti.o', 'crtn.o']:
        #print ("cp {} {}".format(src + "/" + i, dst + "/" + i))
        shutil.copyfile(os.path.join(src, i), os.path.join(dst, i))

    args = ['{}-linux-gnu-gcc'.format(target),
            '-o', '{}/lib/libc.so'.format(sysroot),
            '-nostdlib', '-nostartfiles', '-shared', '-x', 'c',
            '/dev/null']
    run_cmd('Build dummy libc.so for ' + target, args)

    # glibc Stage 2 (final)
    glibc_stage_2_dir = os.path.join(glibc_download_path,
                                     "build-glibc-stage-2." + target)
    os.chdir(glibc_download_path)
    args = ['rm', '-rf', glibc_stage_2_dir]
    run_cmd('clean ' + glibc_stage_2_dir, args)
    os.mkdir(glibc_stage_2_dir)
    os.chdir(glibc_stage_2_dir)

    for i in ['libc.so', 'crt1.o', 'crti.o', 'crtn.o']:
        f = os.path.join(sysroot, 'lib', i)
        if os.path.exists(f):
            os.remove(f)

    args = [glibc_download_path + '/configure',
            '--prefix=/',
            '--exec-prefix=/',
            '--oldincludedir=/include',
            '--with-libdir={}'.format(libdir_path),
            '--target={}-popcorn-linux-gnu'.format(target),
            '--host={}-popcorn-linux-gnu'.format(target),
            'CXX={}-fake-g++'.format(target),
            '--disable-werror',
            '--enable-shared',
            '--enable-obsolete-rpc',
            '--with-headers={}/include'.format(sysroot),
            '--disable-multilib',
            '--enable-kernel=4.4',
            '--with-__thread',
            '--with-tls',
            '--without-cvs',
            '--enable-addons=no',
            '--disable-profile',
            '--without-gd',
            '--without-selinux',
            'libc_cv_slibdir=/lib',
            'libc_cv_rtlddir=/lib',
            'CFLAGS={}'.format("-Og -g3"),
            '--with-nonshared-cflags={}'.format("-ffunction-sections -fdata-sections")]
    run_cmd('Configure glibc Stage 2 for ' + target, args)

    args = ['make', '-j{}'.format(num_threads)]
    run_cmd('Build glibc Stage 2 for ' + target, args)

    args = ['make', '-j{}'.format(num_threads), 'install',
            'install_root={}'.format(sysroot)]
    run_cmd('Build glibc Stage 2 for ' + target, args)

    os.chdir(cur_dir)


def install_glibc(base_path, install_path, install_targets, num_threads):
    cur_dir = os.getcwd()

    glibc_download_path = os.path.join(install_path, 'src', 'glibc')
    linux_download_path = os.path.join(install_path, 'src', 'linux-kernel')

    #TODO: Check whether 'install_path'/src exists.

    if local_glibc:
        # Just build glibc locally
        glibc_download_path = glibc_dev
    else:
        do_git_clone(glibc_url, "release/" + glibc_version + "/master",
                     glibc_download_path, "glibc")

        # Patch GLIBC
        glibc_patch_path = os.path.join(base_path, 'patches', 'glibc',
                                        'glibc-{}.patch'.format(glibc_version))
        with open(glibc_patch_path, 'r') as patch_file:
            print('Patching GLIBC...')
            args = ['patch', '-p1', '-d', glibc_download_path]
            run_cmd('patch GLIBC', args, patch_file)

    # Prepare Linux sources
    if not os.path.exists(linux_download_path):
        args = ['git', 'clone', '--depth', '1', '-b', linux_version, linux_url,
                linux_download_path]
        run_cmd('download Linux Kernel source', args)

    if parallelize_gnu_build:
        jobs = []

        for target in install_targets:
            p = mp.Process(target=do_install_glibc,
                           args=(target, base_path, install_path,
                                 glibc_download_path, num_threads))
            jobs.append(p)

        for j in jobs:
            j.start()

        for j in jobs:
            j.join()
            if j.exitcode != 0:
                print ("install_glibc failed")
                sys.exit(1)

    else:
        for target in install_targets:
            do_install_glibc (target, base_path, install_path,
                              glibc_download_path, num_threads)
            os.chdir(cur_dir)

    os.chdir(cur_dir)

def install_libelf(base_path, install_path, target, num_threads):
    cur_dir = os.getcwd()

    #=====================================================
    # CONFIGURE & INSTALL LIBELF
    #=====================================================
    target_install_path = os.path.join(install_path, target)
    os.chdir(os.path.join(base_path, 'lib', 'libelf'))

    if os.path.isfile('Makefile'):
        run_cmd('clean libelf', ['make', 'distclean'])

    print("Configuring libelf ({})...".format(target))
    compiler = os.path.join(install_path, 'bin',
                            target + '-popcorn-linux-gnu-gcc')
    libelf_cflags = "-O3 -fPIE"

    args = ' '.join(['CC={}'.format(compiler),
                     'CFLAGS="{}"'.format(libelf_cflags),
                     'LDFLAGS="-static"',
                     './configure',
                     '--host={}-popcorn-linux-gnu'.format(target),
                     '--prefix={}'.format(target_install_path),
                     '--enable-compat',
                     '--enable-elf64',
                     '--disable-shared',
                     '--disable-nls',
                     '--enable-extended-format'])


    if (platform.machine() != target):
        args += '--build={}-popcorn-linux-gnu'.format(platform.machine())

    run_cmd('configure libelf ({})'.format(target), args, use_shell=True)

    print('Making libelf ({})...'.format(target))
    args = ['make', '-j', str(num_threads)]
    run_cmd('make libelf ({})'.format(target), args)
    args += ['install']
    run_cmd('install libelf ({})'.format(target), args)

    os.chdir(cur_dir)

def install_libopenpop(base_path, install_path, target, first_target, num_threads):
    global host

    cur_dir = os.getcwd()

    #=====================================================
    # CONFIGURE & INSTALL LIBOPENPOP
    #=====================================================
    target_install_path = os.path.join(install_path, target)
    os.chdir(os.path.join(base_path, 'lib', 'libopenpop'))
    if os.path.isfile('Makefile'):
        run_cmd('clean libopenpop', ['make', 'distclean'])

    print("Configuring libopenpop ({})...".format(target))

    # Get the libgcc file name
    # NOTE: this won't be needed if we force clang to emit 64-bit doubles when
    # compiling musl (currently, it needs soft-FP emulation for 128-bit long
    # doubles)
    args = ['{}-linux-gnu-gcc'.format(target), '-print-libgcc-file-name']
    libgcc = get_cmd_output('get libgcc file name ({})'.format(target), args)
    libgcc = libgcc.strip()

    # NOTE: for build repeatability, we have to use the *same* include path for
    # *all* targets.  This will be unnecessary with unified headers.
    include_dir = os.path.join(install_path, first_target, 'include')
    lib_dir = os.path.join(target_install_path, 'lib')

    args = ' '.join(['CC={}/bin/clang'.format(install_path),
                     'CFLAGS="-target {}-popcorn-linux-gnu -O2 -g -Wall -fno-common ' \
                             '-isystem {} ' \
                             '-popcorn-metadata ' \
                             '-popcorn-target={}-linux-gnu"' \
                             .format(target, include_dir, target),
                     'LDFLAGS="-L{} -B{} --sysroot={}"'
                               .format(lib_dir,cross_path,
                                       target_install_path),
                     'LIBS="-lc {}"'.format(libgcc),
                     './configure',
                     '--prefix={}'.format(target_install_path),
                     '--target={}-linux-gnu'.format(target),
                     '--host={}-linux-gnu'.format(target),
                     '--enable-static',
                     '--disable-shared'])
    run_cmd('configure libopenpop ({})'.format(target), args, use_shell=True)

    print('Making libopenpop ({})...'.format(target))
    args = ['make', '-j', str(num_threads)]
    run_cmd('make libopenpop ({})'.format(target), args)
    args += ['install']
    run_cmd('install libopenpop ({})'.format(target), args)

    os.chdir(cur_dir)

def install_stack_transformation(base_path, install_path, num_threads, st_debug):
    cur_dir = os.getcwd()

    #=====================================================
    # CONFIGURE & INSTALL STACK TRANSFORMATION LIBRARY
    #=====================================================
    os.chdir(os.path.join(base_path, 'lib', 'stack_transformation'))
    run_cmd('clean libstack-transform', ['make', 'clean'])

    print('Making stack_transformation...')
    args = ['make', '-j', str(num_threads), 'POPCORN={}'.format(install_path)]
    if st_debug: args += ['type=debug']
    run_cmd('make libstack-transform', args)
    args += ['install']
    run_cmd('install libstack-transform', args)

    os.chdir(cur_dir)

def install_migration(base_path, install_path, num_threads, libmigration_type,
                      enable_libmigration_timing):
    cur_dir = os.getcwd()

    #=====================================================
    # CONFIGURE & INSTALL MIGRATION LIBRARY
    #=====================================================
    os.chdir(os.path.join(base_path, 'lib', 'migration'))
    run_cmd('clean libmigration', ['make', 'clean'])

    print('Making libmigration...')
    args = ['make',
            '-j', str(num_threads),
            'POPCORN={}'.format(install_path),
            'LLVM_VERSION={}'.format(llvm_version)]
    if libmigration_type or enable_libmigration_timing:
        flags = 'type='
        if libmigration_type and enable_libmigration_timing:
            flags += libmigration_type + ',timing'
        elif libmigration_type:
            flags += libmigration_type
        elif enable_libmigration_timing:
            flags += 'timing'
        args += [flags]
    run_cmd('make migration', args)
    args += ['install']
    run_cmd('install migration', args)

    os.chdir(cur_dir)

def install_remote_io(base_path, install_path, num_threads, rio_debug):
    cur_dir = os.getcwd()

    #=====================================================
    # CONFIGURE & INSTALL STACK TRANSFORMATION LIBRARY
    #=====================================================
    os.chdir(os.path.join(base_path, 'lib', 'remote_io'))
    run_cmd('clean libremote_io', ['make', 'clean'])

    print('Making remote_io...')
    args = ['make', '-j', str(num_threads), 'POPCORN={}'.format(install_path)]
    if rio_debug: args += ['type=debug']
    run_cmd('make libremote_io', args)
    args += ['install']
    run_cmd('install libremote_io', args)

    os.chdir(cur_dir)

def do_install_git(install_path, threads, name, url, branch, dir, autogen, cfg):
    src_dir = os.path.join(install_path, "src", name)
    do_git_clone(url, branch, src_dir, name)
    os.chdir(src_dir)
    sysroot_arm = os.path.join(install_path, "aarch64")
    sysroot_x86 = os.path.join(install_path, "x86_64")

    if autogen:
        args = [autogen]
        run_cmd('Running autogen.sh for {}'.format(name), args)

    arm_cfg = ['./configure',
               '--target=aarch64-popcorn-linux-gnu',
               'cross_compiling=yes',
               'CC={}/bin/clang'.format(install_path),
               'CFLAGS=-O0 -g3 -ffunction-sections -fdata-sections -mllvm -popcorn-instrument=metadata -mllvm -optimize-regalloc -mllvm -fast-isel=false --target=aarch64-popcorn-linux-gnu --gcc-toolchain={} --sysroot={}/aarch64'.format(install_path, install_path),
               'LD=clang --target=aarch64-popcorn-linux-gnu --gcc-toolchain={}'.format(install_path),
               '--disable-shared',
               '--prefix=/']
    arm_cfg += cfg
    run_cmd('Configuring {} for ARM'.format(name), arm_cfg)

    build_cmd = ['make', '-j{}'.format(threads), '-C', dir]
    run_cmd("Building '{}' for ARM".format(name), build_cmd)

    args = ['make', '-j{}'.format(threads), 'install',
            'DESTDIR={}'.format(sysroot_arm)]
    run_cmd('Installing {} for ARM', args)

    clean_cmd = ['make', 'clean']
    run_cmd("Cleaning {}".format(name), clean_cmd)

    x86_cfg = ['./configure',
                '--target=x86_64-popcorn-linux-gnu',
               'cross_compiling=yes',
               'CC={}/bin/clang'.format(install_path),
               'CFLAGS=-O0 -g3 -ffunction-sections -fdata-sections -mllvm -popcorn-instrument=metadata -mllvm -optimize-regalloc -mllvm -fast-isel=false --target=x86_64-popcorn-linux-gnu --gcc-toolchain={} --sysroot={}/x86_64'.format(install_path, install_path),
               'LD=clang --target=x86_64-popcorn-linux-gnu --gcc-toolchain={}'.format(install_path),
               '--disable-shared',
               '--prefix=/']
    x86_cfg += cfg
    run_cmd('Configuring {} for X86'.format(name), x86_cfg)

    build_cmd = ['make', '-j{}'.format(threads), '-C', dir]
    run_cmd("Building '{}' for X86".format(name), build_cmd)

    args = ['make', '-j{}'.format(threads), 'install',
            'DESTDIR={}'.format(sysroot_x86)]
    run_cmd('Installing {} for X86', args)

def install_x11_libraries(base_path, install_path, threads):
    # Installing X11 headers
    args = ['tar',
            'xjf',
            '{}/patches/xorg/xorg-headers-ubuntu-2004-arm.tbz'.format(base_path),
            '-C',
            '{}'.format(install_path + "/aarch64/include")]
    run_cmd("Install ARM X11 headers", args)

    args = ['tar',
            'xjf',
            '{}/patches/xorg/xorg-headers-ubuntu-2004-x86.tbz'.format(base_path),
            '-C',
            '{}'.format(install_path + "/x86_64/include")]
    run_cmd("Install X86 X11 headers", args)

    # Install libuuid
    libuuid_cfg = ['--disable-all-programs', '--enable-libuuid']
    do_install_git(install_path, threads, "util-linux", libuuid_url,
                   "v2.37.2", ".", './autogen.sh', libuuid_cfg)

    # Install libmd for libbsd
    do_install_git(install_path, threads, "libmd", libmd_url,
                   "1.0.4", ".", "./autogen", [])

    # Install libbsd
    do_install_git(install_path, threads, "libbsd", libbsd_url,
                   "0.11.5", ".", "./autogen", [])

    # Build the X11 libraries
    libs = [["libxt", "master", "libxt.patch", "src"],
            ["libxaw", "master", "", "src"],
            ["libxmu", "master", "", "src"],
            ["libxext", "master", "", "src"],
            ["libxpm", "master", "", "src"],
            ["libxcb", "libxcb-1.14", "libxcb.patch", "src"],
            ["libxau", "libXau-1.0.10", "", "."],
            ["libxdmcp", "master", "", "."],
            ["libice", "libICE-1.0.10", "", "src"],
            ["libsm", "libSM-1.2.3", "", "src"],
            ["libx11", "libX11-1.7.2", "", "."]]

    for (l, t, p, s) in libs:
        src_dir = os.path.join(install_path, "src", l)
        do_git_clone(xorg_url + l + ".git", t, src_dir, l)
        os.chdir(src_dir)

        if p != "":
            with open('{}/patches/xorg/{}'.format(base_path, p), 'r') as patch:
                args = ['patch',
                        '-p1']
                run_cmd("Patching '{}'".format(l), args, patch)

        args = ['./autogen.sh']
        run_cmd('Running autogen.sh for {}'.format(l), args)

        arm_cfg = ['./configure',
                   '--target=aarch64-popcorn-linux-gnu',
                   'cross_compiling=yes',
                   'CC={}/bin/clang'.format(install_path),
                   'CFLAGS=-O0 -g3 -ffunction-sections -fdata-sections -mllvm -popcorn-instrument=metadata -mllvm -optimize-regalloc -mllvm -fast-isel=false --target=aarch64-popcorn-linux-gnu --gcc-toolchain={} --sysroot={}/aarch64'.format(install_path, install_path),
                   'LD=clang --target=aarch64-popcorn-linux-gnu --gcc-toolchain={}'.format(install_path),
                   'PYTHON=python3',
                   '--disable-shared',
                   '--prefix=/']
        if l != "libXau":
            arm_cfg += ['--enable-malloc0returnsnull']
        if l == "libx11":
            arm_cfg += ['--disable-loadable-xcursor']
        run_cmd('Configuring {} for ARM'.format(l), arm_cfg)

        build_cmd = ['make', '-j{}'.format(threads), '-C', s]
        run_cmd("Building '{}' for ARM".format(l), build_cmd)

        do_copy_file_type('.', install_path + "/aarch64/lib", '*.a')

        clean_cmd = ['make', 'clean']
        run_cmd("Cleaning {}".format(l), clean_cmd)

        x86_cfg = ['./configure',
                   '--target=x86_64-popcorn-linux-gnu',
                   'cross_compiling=yes',
                   'CC={}/bin/clang'.format(install_path),
                   'CFLAGS=-O0 -g3 -ffunction-sections -fdata-sections -mllvm -popcorn-instrument=metadata -mllvm -optimize-regalloc -mllvm -fast-isel=false --target=x86_64-popcorn-linux-gnu --gcc-toolchain={} --sysroot={}/x86_64'.format(install_path, install_path),
                   'LD=clang --target=x86_64-popcorn-linux-gnu --gcc-toolchain={}'.format(install_path),
                   'PYTHON=python3',
                   '--disable-shared',
                   '--prefix=/']
        if l != "libXau":
            x86_cfg += ['--enable-malloc0returnsnull']
        if l == "libx11":
            x86_cfg += ['--disable-loadable-xcursor']
        run_cmd('Configuring {} for X86'.format(l), x86_cfg)

        build_cmd = ['make', '-j{}'.format(threads), '-C', s]
        run_cmd("Building '{}' for X86".format(l), build_cmd)

        do_copy_file_type('.', install_path + "/x86_64/lib", '*.a')


def install_stackdepth(base_path, install_path, num_threads):
    cur_dir = os.getcwd()

    #=====================================================
    # INSTALL STACK DEPTH LIBRARY
    #=====================================================
    os.chdir(os.path.join(base_path, 'lib', 'stack_depth'))
    run_cmd('clean libstack-depth', ['make', 'clean'])

    print('Making stack depth library...')
    args = ['make', '-j', str(num_threads), 'POPCORN={}'.format(install_path)]
    run_cmd('make libstack-depth', args)
    args += ['install']
    run_cmd('install libstack-depth', args)

    os.chdir(cur_dir)

def install_tools(base_path, install_path, num_threads):
    cur_dir = os.getcwd()

    #=====================================================
    # INSTALL ALIGNMENT TOOL
    #=====================================================
    os.chdir(os.path.join(base_path, 'tool', 'alignment'))
    run_cmd('clean pyalign', ['make', 'clean'])

    print('Making pyalign...')
    args = ['make', '-j', str(num_threads), 'install',
            'POPCORN={}'.format(install_path)]
    run_cmd('make/install pyalign', args)

    os.chdir(cur_dir)

    #=====================================================
    # INSTALL STACK METADATA TOOL
    #=====================================================
    os.chdir(os.path.join(base_path, 'tool', 'stack_metadata'))
    run_cmd('clean stack metadata tool', ['make', 'clean'])

    print('Making stack metadata tool...')
    args = ['make', '-j', str(num_threads), 'POPCORN={}'.format(install_path)]
    run_cmd('make stack metadata tools', args)
    args += ['install']
    run_cmd('install stack metadata tools', args)

    os.chdir(cur_dir)

def install_utils(base_path, install_path, num_threads):
    cur_dir = os.getcwd()

    #=====================================================
    # MODIFY MAKEFILE TEMPLATE
    #=====================================================
    print("Updating util/Makefile.template to reflect install path...")

    os.chdir (base_path)
    tmp = install_path.replace('/', '\/')
    sed_cmd = "sed -i -e 's/^POPCORN ?= .*/POPCORN ?= {}/g' " \
              "./util/Makefile.template".format(tmp)
    run_cmd('update Makefile template', sed_cmd, use_shell=True)

    #=====================================================
    # COPY SCRIPTS
    #=====================================================
    print("Copying util/scripts to {}/bin...".format(install_path))
    for item in os.listdir('./util/scripts'):
        s = os.path.join('./util', 'scripts', item)
        d = os.path.join(os.path.join(install_path, 'bin'), item)
        if item != 'README':
            shutil.copy(s, d)

    #=====================================================
    # PREPARE SETPATH SCRIPT
    #=====================================================
    setpath = """export POPCORN={}

DEPS=${{HOME}}/rtl/popcorn/deps/inst
export PATH=${{POPCORN}}/bin:${{POPCORN}}/x86_64/bin:${{DEPS}}/bin:$PATH
"""

    f = open (install_path + "/setpath", "w")
    f.write (setpath.format(install_path))
    f.close ()
    os.chdir (cur_dir)


def build_namespace(base_path):
    cur_dir = os.getcwd()

    #=====================================================
    # MAKE NAMESPACE
    #=====================================================
    os.chdir(os.path.join(base_path, 'tool', 'namespace'))

    print("Building namespace tools...")
    run_cmd('make namespace tools', ['make'])

    os.chdir(cur_dir)

def clean_build(install_path):
    llvm_path = os.path.join(install_path, 'src/llvm/llvm/build')
    gcc_path = os.path.join(install_path, 'src/gcc')
    binutils_path = os.path.join(install_path, 'src/binutils-2.32')
    glibc_path = os.path.join(install_path, 'src/glibc')

    if os.path.exists(llvm_path):
        shutil.rmtree (llvm_path)

    dirs = os.listdir(gcc_path)
    for d in dirs:
        if re.search("build*", d):
            shutil.rmtree (os.path.join (gcc_path, d))

    dirs = os.listdir(binutils_path)
    for d in dirs:
        if re.search("build*", d):
            shutil.rmtree (os.path.join (binutils_path, d))

    dirs = os.listdir(glibc_path)
    for d in dirs:
        if re.search("build*", d):
            shutil.rmtree (os.path.join (glibc_path, d))

def main(args):

    if args.llvm_clang_install:
        install_clang_llvm(args.base_path, args.install_path, args.threads,
                           args.llvm_targets)

    if args.binutils_install:
        install_binutils(args.base_path, args.install_path, args.threads,
                         args.install_targets)

    if args.gcc_glibc_install:
        install_gcc_glibc(args.base_path, args.install_path,
                          args.install_targets, args.threads)

    if args.glibc_install:
        install_glibc(args.base_path, args.install_path,
                      args.install_targets, args.threads)

    for target in args.install_targets:
        if args.musl_install:
            install_musl(args.base_path, args.install_path, target,
                         args.threads)

        if args.libelf_install:
            install_libelf(args.base_path, args.install_path, target,
                           args.threads)

    if args.remote_io_install:
        install_remote_io(args.base_path, args.install_path,
                          args.threads,
                          args.debug_remote_io)

    if args.stacktransform_install:
        install_stack_transformation(args.base_path, args.install_path,
                                     args.threads,
                                     args.debug_stack_transformation)

    if args.migration_install:
        install_migration(args.base_path, args.install_path, args.threads,
                          args.libmigration_type,
                          args.enable_libmigration_timing)

    if args.x11_install:
        install_x11_libraries(args.base_path, args.install_path, args.threads)

    if args.libopenpop_install:
        for target in args.install_targets:
            install_libopenpop(args.base_path, args.install_path, target,
                               args.install_targets[0], args.threads)

    if args.stackdepth_install:
        install_stackdepth(args.base_path, args.install_path, args.threads)

    if args.tools_install:
        install_tools(args.base_path, args.install_path, args.threads)

    if args.utils_install:
        install_utils(args.base_path, args.install_path, args.threads)

    if args.clean_install:
        clean_build(args.install_path)

    if args.namespace_install:
        build_namespace(args.base_path)

if __name__ == '__main__':
    parser = setup_argument_parsing()
    args = parser.parse_args()
    postprocess_args(args)
    warn_stupid(args)

    if not args.skip_prereq_check:
        success = check_for_prerequisites(args)
        if success != True:
            print('All prerequisites were not satisfied!')
            sys.exit(1)
        print('All prerequisites were satisified :)')

    main(args)
