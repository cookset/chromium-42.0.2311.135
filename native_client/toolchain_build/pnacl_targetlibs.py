#!/usr/bin/python
# Copyright (c) 2013 The Native Client Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Recipes for PNaCl target libs."""

import fnmatch
import os
import sys

sys.path.append(os.path.join(os.path.dirname(__file__), '..'))
import pynacl.gsd_storage
import pynacl.platform

import command
import pnacl_commands

from toolchain_build import NewlibLibcScript

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
NACL_DIR = os.path.dirname(SCRIPT_DIR)

CLANG_VER = '3.5.0'

# Return the path to a tool to build target libraries
# msys should be false if the path will be called directly rather than passed to
# an msys or cygwin tool such as sh or make.
def PnaclTool(toolname, arch='le32', msys=True):
  if not msys and pynacl.platform.IsWindows():
    ext = '.bat'
  else:
    ext = ''
  if IsBCArch(arch):
    base = 'pnacl-' + toolname
  else:
    base = '-'.join([TargetArch(arch), 'nacl', toolname])
  return command.path.join('%(abs_target_lib_compiler)s',
                           'bin', base + ext)

# PNaCl tools for newlib's environment, e.g. CC_FOR_TARGET=/path/to/pnacl-clang
TOOL_ENV_NAMES = { 'CC': 'clang', 'CXX': 'clang++', 'AR': 'ar', 'NM': 'nm',
                   'RANLIB': 'ranlib', 'READELF': 'readelf',
                   'OBJDUMP': 'illegal', 'AS': 'as', 'LD': 'illegal',
                   'STRIP': 'illegal' }

def TargetTools(arch):
  return [ tool + '_FOR_TARGET=' + PnaclTool(name, arch=arch, msys=True)
           for tool, name in TOOL_ENV_NAMES.iteritems() ]


def MakeCommand():
  make_command = ['make']
  if not pynacl.platform.IsWindows():
    # The make that ships with msys sometimes hangs when run with -j.
    # The ming32-make that comes with the compiler itself reportedly doesn't
    # have this problem, but it has issues with pathnames with LLVM's build.
    make_command.append('-j%(cores)s')
  return make_command

# Return the component name to use for a component name with
# a host triple. GNU configuration triples contain dashes, which are converted
# to underscores so the names are legal for Google Storage.
def GSDJoin(*args):
  return '_'.join([pynacl.gsd_storage.LegalizeName(arg) for arg in args])


def TripleFromArch(bias_arch):
  return bias_arch + '-nacl'


def IsBiasedBCArch(arch):
  return arch.endswith('_bc')


def IsBCArch(arch):
  return IsBiasedBCArch(arch) or arch == 'le32'


# Return the target arch recognized by the compiler (e.g. i686) from an arch
# string which might be a biased-bitcode-style arch (e.g. i686_bc)
def TargetArch(bias_arch):
  if IsBiasedBCArch(bias_arch):
    return bias_arch[:bias_arch.index('_bc')]
  return bias_arch


def MultilibArch(bias_arch):
  return 'x86_64' if bias_arch == 'i686' else bias_arch


def MultilibLibDir(bias_arch):
  suffix = '32' if bias_arch == 'i686' else ''
  return os.path.join(TripleFromArch(MultilibArch(bias_arch)), 'lib' + suffix)


def BiasedBitcodeTargetFlag(bias_arch):
  arch = TargetArch(bias_arch)
  flagmap = {
      # Arch     Target                           Extra flags.
      'x86_64': ('x86_64-unknown-nacl',           []),
      'i686':   ('i686-unknown-nacl',             []),
      'arm':    ('armv7-unknown-nacl-gnueabihf',  ['-mfloat-abi=hard']),
      # MIPS doesn't use biased bitcode:
      'mips32': ('le32-unknown-nacl',             []),
  }
  return ['--target=%s' % flagmap[arch][0]] + flagmap[arch][1]


def TargetLibCflags(bias_arch):
  flags = '-g -O2'
  if IsBCArch(bias_arch):
    flags += ' -mllvm -inline-threshold=5 -allow-asm'
  if IsBiasedBCArch(bias_arch):
    flags += ' ' + ' '.join(BiasedBitcodeTargetFlag(bias_arch))
  return flags


def NewlibIsystemCflags(bias_arch):
  include_arch = MultilibArch(bias_arch)
  return ' '.join([
    '-isystem',
    command.path.join('%(' + GSDJoin('abs_newlib', include_arch) +')s',
                      TripleFromArch(include_arch), 'include')])

def LibCxxCflags(bias_arch):
  # HAS_THREAD_LOCAL is used by libc++abi's exception storage, the fallback is
  # pthread otherwise.
  return ' '.join([TargetLibCflags(bias_arch), NewlibIsystemCflags(bias_arch),
                   '-DHAS_THREAD_LOCAL=1', '-D__ARM_DWARF_EH__'])


def LibStdcxxCflags(bias_arch):
  return ' '.join([TargetLibCflags(bias_arch),
                   NewlibIsystemCflags(bias_arch)])


# Build a single object file for the target.
def BuildTargetObjectCmd(source, output, bias_arch, output_dir='%(cwd)s'):
  flags = ['-Wall', '-Werror', '-O2', '-c']
  if IsBiasedBCArch(bias_arch):
    flags.extend(BiasedBitcodeTargetFlag(bias_arch))
  flags.extend(NewlibIsystemCflags(bias_arch).split())
  return command.Command(
      [PnaclTool('clang', arch=bias_arch, msys=False)] + flags + [
          command.path.join('%(src)s', source),
     '-o', command.path.join(output_dir, output)])


# Build a single object file as native code for the translator.
def BuildTargetTranslatorCmd(sourcefile, output, arch, extra_flags=[],
                             source_dir='%(src)s', output_dir='%(cwd)s'):
  return command.Command(
    [PnaclTool('clang', msys=False),
     '--pnacl-allow-native', '--pnacl-allow-translate', '-Wall', '-Werror',
     '-arch', arch, '--pnacl-bias=' + arch, '-O3',
     # TODO(dschuff): this include breaks the input encapsulation for build
     # rules.
     '-I%(top_srcdir)s/..','-c'] +
    NewlibIsystemCflags('le32').split() +
    extra_flags +
    [command.path.join(source_dir, sourcefile),
     '-o', command.path.join(output_dir, output)])


def BuildLibgccEhCmd(sourcefile, output, arch):
  # Return a command to compile a file from libgcc_eh (see comments in at the
  # rule definition below).
  flags_common = ['-DENABLE_RUNTIME_CHECKING', '-g', '-O2', '-W', '-Wall',
                  '-Wwrite-strings', '-Wcast-qual', '-Wstrict-prototypes',
                  '-Wmissing-prototypes', '-Wold-style-definition',
                  '-DIN_GCC', '-DCROSS_DIRECTORY_STRUCTURE', '-DIN_LIBGCC2',
                  '-D__GCC_FLOAT_NOT_NEEDED', '-Dinhibit_libc',
                  '-DHAVE_CC_TLS', '-DHIDE_EXPORTS',
                  '-fno-stack-protector', '-fexceptions',
                  '-fvisibility=hidden',
                  '-I.', '-I../.././gcc', '-I%(abs_gcc_src)s/gcc/libgcc',
                  '-I%(abs_gcc_src)s/gcc', '-I%(abs_gcc_src)s/include',
                  '-isystem', './include']
  # For x86 we use nacl-gcc to build libgcc_eh because of some issues with
  # LLVM's handling of the gcc intrinsics used in the library. See
  # https://code.google.com/p/nativeclient/issues/detail?id=1933
  # and http://llvm.org/bugs/show_bug.cgi?id=8541
  # For ARM, LLVM does work and we use it to avoid dealing with the fact that
  # arm-nacl-gcc uses different libgcc support functions than PNaCl.
  if arch in ('arm', 'mips32'):
    cc = PnaclTool('clang', msys=False)
    flags_naclcc = ['-arch', arch, '--pnacl-bias=' + arch,
                    '--pnacl-allow-translate', '--pnacl-allow-native']
  else:
    os_name = pynacl.platform.GetOS()
    arch_name = pynacl.platform.GetArch()
    platform_dir = '%s_%s' % (os_name, arch_name)
    newlib_dir = 'nacl_x86_newlib_raw'

    nnacl_dir = os.path.join(NACL_DIR, 'toolchain', platform_dir,
                             newlib_dir, 'bin')
    gcc_binaries = {
        'x86-32': 'i686-nacl-gcc',
        'x86-64': 'x86_64-nacl-gcc',
    }

    cc = os.path.join(nnacl_dir, gcc_binaries[arch])
    flags_naclcc = []
  return command.Command([cc] + flags_naclcc + flags_common +
                         ['-c',
                          command.path.join('%(gcc_src)s', 'gcc', sourcefile),
                          '-o', output])



def TargetLibsSrc(GitSyncCmds):
  newlib_sys_nacl = command.path.join('%(output)s',
                                      'newlib', 'libc', 'sys', 'nacl')
  source = {
      'newlib_src': {
          'type': 'source',
          'output_dirname': 'pnacl-newlib',
          'commands': [
              # Clean any headers exported from the NaCl tree before syncing.
              command.CleanGitWorkingDir(
                  '%(output)s',
                  reset=False,
                  path=os.path.join('newlib', 'libc', 'include'))] +
              GitSyncCmds('nacl-newlib') +
              # Remove newlib versions of headers that will be replaced by
              # headers from the NaCl tree.
              [command.RemoveDirectory(command.path.join(newlib_sys_nacl,
                                                         dirname))
               for dirname in ['bits', 'sys', 'machine']] + [
              command.Command([
                  sys.executable,
                  command.path.join('%(top_srcdir)s', 'src', 'trusted',
                                    'service_runtime', 'export_header.py'),
                  command.path.join('%(top_srcdir)s', 'src', 'trusted',
                                    'service_runtime', 'include'),
                  newlib_sys_nacl],
                  cwd='%(abs_output)s',
              )] + [
              command.Copy(
                  os.path.join('%(top_srcdir)s', 'src', 'untrusted', 'pthread',
                               header),
                  os.path.join('%(output)s', 'newlib', 'libc', 'include',
                               header))
              for header in ('pthread.h', 'semaphore.h')
       ]
      },
      'compiler_rt_src': {
          'type': 'source',
          'output_dirname': 'compiler-rt',
          'commands': GitSyncCmds('compiler-rt'),
      },
      'gcc_src': {
          'type': 'source',
          'output_dirname': 'pnacl-gcc',
          'commands': GitSyncCmds('gcc'),
      },
  }
  return source


def NewlibDirectoryCmds(bias_arch, newlib_triple):
  commands = []
  def NewlibLib(name):
    return os.path.join('%(output)s', newlib_triple, 'lib', name)
  if not IsBCArch(bias_arch):
    commands.extend([
      command.Rename(NewlibLib('libc.a'), NewlibLib('libcrt_common.a')),
      command.WriteData(NewlibLibcScript(bias_arch, 'elf64'),
                        NewlibLib('libc.a'))])
  target_triple = TripleFromArch(bias_arch)
  if bias_arch != 'i686':
    commands.extend([
        # For biased bitcode builds, we configured newlib with target=le32-nacl
        # to get its pure C implementation, so rename its output dir (which
        # matches the target to the output dir for the package we are building)
        command.Rename(os.path.join('%(output)s', newlib_triple),
                       os.path.join('%(output)s', target_triple)),
        # Copy nacl_random.h, used by libc++. It uses the IRT, so should
        # be safe to include in the toolchain.
        command.Mkdir(
            os.path.join('%(output)s', target_triple, 'include', 'nacl')),
        command.Copy(os.path.join('%(top_srcdir)s', 'src', 'untrusted',
                                  'nacl', 'nacl_random.h'),
                     os.path.join('%(output)s', target_triple, 'include',
                                  'nacl', 'nacl_random.h'))
    ])
  else:
    # Use multilib-style directories for i686
    multilib_triple = TripleFromArch(MultilibArch(bias_arch))
    commands.extend([
        command.Rename(os.path.join('%(output)s', newlib_triple),
                       os.path.join('%(output)s', multilib_triple)),
        command.RemoveDirectory(
            os.path.join('%(output)s', multilib_triple, 'include')),
        command.Rename(
            os.path.join('%(output)s', multilib_triple, 'lib'),
            os.path.join('%(output)s', multilib_triple, 'lib32')),
    ])
  # Remove the 'share' directory from the biased builds; the data is
  # duplicated exactly and takes up 2MB per package.
  if bias_arch != 'le32':
    commands.append(command.RemoveDirectory(os.path.join('%(output)s','share')))
  return commands


def LibcxxDirectoryCmds(bias_arch):
  if bias_arch != 'i686':
    return []
  lib_dir = os.path.join('%(output)s', MultilibLibDir(bias_arch))
  return [
      # Use the multlib-style lib dir and shared headers for i686
      command.Mkdir(os.path.dirname(lib_dir)),
      command.Rename(os.path.join('%(output)s', 'i686-nacl', 'lib'),
                     lib_dir),
      # The only thing left in i686-nacl is the headers
      command.RemoveDirectory(os.path.join('%(output)s', 'i686-nacl')),
  ]


def TargetLibBuildType(is_canonical):
  return 'build' if is_canonical else 'build_noncanonical'

def TargetLibs(bias_arch, is_canonical):
  def T(component_name):
    return GSDJoin(component_name, bias_arch)
  target_triple = TripleFromArch(bias_arch)
  newlib_triple = target_triple if not IsBCArch(bias_arch) else 'le32-nacl'
  newlib_cpp_flags = ' -DPNACL_BITCODE' if IsBCArch(bias_arch) else ''
  clang_libdir = os.path.join(
      '%(output)s', 'lib', 'clang', CLANG_VER, 'lib', target_triple)
  libc_libdir = os.path.join('%(output)s', MultilibLibDir(bias_arch))
  libs = {
      T('newlib'): {
          'type': TargetLibBuildType(is_canonical),
          'dependencies': [ 'newlib_src', 'target_lib_compiler'],
          'commands' : [
              command.SkipForIncrementalCommand(
                  ['sh', '%(newlib_src)s/configure'] +
                  TargetTools(bias_arch) +
                  ['CFLAGS_FOR_TARGET=' +
                      TargetLibCflags(bias_arch) +
                      newlib_cpp_flags,
                  '--prefix=',
                  '--disable-newlib-supplied-syscalls',
                  '--disable-texinfo',
                  '--disable-libgloss',
                  '--enable-newlib-iconv',
                  '--enable-newlib-iconv-from-encodings=' +
                  'UTF-8,UTF-16LE,UCS-4LE,UTF-16,UCS-4',
                  '--enable-newlib-iconv-to-encodings=' +
                  'UTF-8,UTF-16LE,UCS-4LE,UTF-16,UCS-4',
                  '--enable-newlib-io-long-long',
                  '--enable-newlib-io-long-double',
                  '--enable-newlib-io-c99-formats',
                  '--enable-newlib-mb',
                  '--target=' + newlib_triple
              ]),
              command.Command(MakeCommand()),
              command.Command(['make', 'DESTDIR=%(abs_output)s', 'install']),
          ] + NewlibDirectoryCmds(bias_arch, newlib_triple)
      },
      T('libcxx'): {
          'type': TargetLibBuildType(is_canonical),
          'dependencies': ['libcxx_src', 'libcxxabi_src', 'llvm_src', 'gcc_src',
                           'target_lib_compiler', T('newlib'),
                           GSDJoin('newlib', MultilibArch(bias_arch)),
                           T('libs_support')],
          'commands' :
              [command.SkipForIncrementalCommand(
                  ['cmake', '-G', 'Unix Makefiles',
                   '-DCMAKE_C_COMPILER_WORKS=1',
                   '-DCMAKE_CXX_COMPILER_WORKS=1',
                   '-DCMAKE_INSTALL_PREFIX=',
                   '-DCMAKE_BUILD_TYPE=Release',
                   '-DCMAKE_C_COMPILER=' + PnaclTool('clang', bias_arch),
                   '-DCMAKE_CXX_COMPILER=' + PnaclTool('clang++', bias_arch),
                   '-DCMAKE_SYSTEM_NAME=nacl',
                   '-DCMAKE_AR=' + PnaclTool('ar', bias_arch),
                   '-DCMAKE_NM=' + PnaclTool('nm', bias_arch),
                   '-DCMAKE_RANLIB=' + PnaclTool('ranlib', bias_arch),
                   '-DCMAKE_LD=' + PnaclTool('illegal', bias_arch),
                   '-DCMAKE_AS=' + PnaclTool('as', bias_arch),
                   '-DCMAKE_OBJDUMP=' + PnaclTool('illegal', bias_arch),
                   '-DCMAKE_C_FLAGS=-std=gnu11 ' + LibCxxCflags(bias_arch),
                   '-DCMAKE_CXX_FLAGS=-std=gnu++11 ' + LibCxxCflags(bias_arch),
                   '-DLIT_EXECUTABLE=' + command.path.join(
                       '%(llvm_src)s', 'utils', 'lit', 'lit.py'),
                   # The lit flags are used by the libcxx testsuite, which is
                   # currenty driven by an external script.
                   '-DLLVM_LIT_ARGS=--verbose  --param shell_prefix="' +
                    os.path.join(NACL_DIR,'run.py') +' -arch env --retries=1" '+
                    '--param exe_suffix=".pexe" --param use_system_lib=true ' +
                    '--param cxx_under_test="' + os.path.join(NACL_DIR,
                        'toolchain/linux_x86/pnacl_newlib',
                        'bin/pnacl-clang++') +
                    '" '+
                    '--param link_flags="-std=gnu++11 --pnacl-exceptions=sjlj"',
                   '-DLIBCXX_ENABLE_CXX0X=0',
                   '-DLIBCXX_ENABLE_SHARED=0',
                   '-DLIBCXX_CXX_ABI=libcxxabi',
                   '-DLIBCXX_LIBCXXABI_INCLUDE_PATHS=' + command.path.join(
                       '%(abs_libcxxabi_src)s', 'include'),
                   '%(libcxx_src)s']),
              command.Copy(os.path.join('%(gcc_src)s', 'gcc',
                                        'unwind-generic.h'),
                           os.path.join('include', 'unwind.h')),
              command.Command(MakeCommand() + ['VERBOSE=1']),
              command.Command([
                  'make',
                  'DESTDIR=' + os.path.join('%(abs_output)s', target_triple),
                  'VERBOSE=1',
                  'install']),
          ] + LibcxxDirectoryCmds(bias_arch)
      },
      T('libstdcxx'): {
          'type': TargetLibBuildType(is_canonical),
          'dependencies': ['gcc_src', 'gcc_src', 'target_lib_compiler',
                           T('newlib')],
          'commands' : [
              command.SkipForIncrementalCommand([
                  'sh',
                  command.path.join('%(gcc_src)s', 'libstdc++-v3',
                                    'configure')] +
                  TargetTools(bias_arch) + [
                  'CC_FOR_BUILD=cc',
                  'CC=' + PnaclTool('clang'),
                  'CXX=' + PnaclTool('clang++'),
                  'AR=' + PnaclTool('ar'),
                  'NM=' + PnaclTool('nm'),
                  'RAW_CXX_FOR_TARGET=' + PnaclTool('clang++'),
                  'LD=' + PnaclTool('illegal'),
                  'RANLIB=' + PnaclTool('ranlib'),
                  'CFLAGS=' + LibStdcxxCflags(bias_arch),
                  'CXXFLAGS=' + LibStdcxxCflags(bias_arch),
                  'CPPFLAGS=' + NewlibIsystemCflags(bias_arch),
                  'CFLAGS_FOR_TARGET=' + LibStdcxxCflags(bias_arch),
                  'CXXFLAGS_FOR_TARGET=' + LibStdcxxCflags(bias_arch),
                  '--host=arm-none-linux-gnueabi',
                  '--prefix=',
                  '--enable-cxx-flags=-D__SIZE_MAX__=4294967295',
                  '--disable-multilib',
                  '--disable-linux-futex',
                  '--disable-libstdcxx-time',
                  '--disable-sjlj-exceptions',
                  '--disable-libstdcxx-pch',
                  '--with-newlib',
                  '--disable-shared',
                  '--disable-rpath']),
              command.Copy(os.path.join('%(gcc_src)s', 'gcc',
                                        'unwind-generic.h'),
                           os.path.join('include', 'unwind.h')),
              command.Command(MakeCommand()),
              command.Command([
                  'make',
                  'DESTDIR=' + os.path.join('%(abs_output)s', target_triple),
                  'install-data']),
              command.RemoveDirectory(
                  os.path.join('%(output)s', target_triple, 'share')),
              command.Remove(os.path.join('%(output)s', target_triple, 'lib',
                                          'libstdc++*-gdb.py')),
              command.Copy(
                  os.path.join('src', '.libs', 'libstdc++.a'),
                  os.path.join('%(output)s', target_triple, 'lib',
                               'libstdc++.a')),
          ],
      },
  }
  if IsBCArch(bias_arch):
    libs.update({
      T('compiler_rt_bc'): {
          'type': TargetLibBuildType(is_canonical),
          'dependencies': ['compiler_rt_src', 'target_lib_compiler'],
          'commands': [
              command.Mkdir(clang_libdir, parents=True),
              command.Command(MakeCommand() + [
                  '-f',
                  command.path.join('%(compiler_rt_src)s', 'lib', 'builtins',
                                    'Makefile-pnacl-bitcode'),
                  'libgcc.a', 'CC=' + PnaclTool('clang'),
                  'AR=' + PnaclTool('ar')] +
                  ['SRC_DIR=' + command.path.join('%(abs_compiler_rt_src)s',
                                                  'lib', 'builtins'),
                   'CFLAGS=' + ' '.join([
                     '-DPNACL_' + TargetArch(bias_arch).replace('-', '_')])
                  ]),
              command.Copy('libgcc.a', os.path.join(clang_libdir, 'libgcc.a')),
          ],
      },
      T('libs_support'): {
          'type': TargetLibBuildType(is_canonical),
          'dependencies': [ T('newlib'), 'target_lib_compiler'],
          'inputs': { 'src': os.path.join(NACL_DIR,
                                          'pnacl', 'support', 'bitcode')},
          'commands': [
              command.Mkdir(clang_libdir, parents=True),
              command.Mkdir(libc_libdir, parents=True),
              # Two versions of crt1.x exist, for different scenarios (with and
              # without EH).  See:
              # https://code.google.com/p/nativeclient/issues/detail?id=3069
              command.Copy(command.path.join('%(src)s', 'crt1.x'),
                           command.path.join(libc_libdir, 'crt1.x')),
              command.Copy(command.path.join('%(src)s', 'crt1_for_eh.x'),
                           command.path.join(libc_libdir, 'crt1_for_eh.x')),
              # Install crti.bc (empty _init/_fini)
              BuildTargetObjectCmd('crti.c', 'crti.bc', bias_arch,
                                    output_dir=libc_libdir),
              # Install crtbegin bitcode (__cxa_finalize for C++)
              BuildTargetObjectCmd('crtbegin.c', 'crtbegin.bc', bias_arch,
                                    output_dir=clang_libdir),
              # Stubs for _Unwind_* functions when libgcc_eh is not included in
              # the native link).
              BuildTargetObjectCmd('unwind_stubs.c', 'unwind_stubs.bc',
                                    bias_arch, output_dir=clang_libdir),
              BuildTargetObjectCmd('sjlj_eh_redirect.cc',
                                    'sjlj_eh_redirect.bc', bias_arch,
                                    output_dir=clang_libdir),
              # libpnaclmm.a (__atomic_* library functions).
              BuildTargetObjectCmd('pnaclmm.c', 'pnaclmm.bc', bias_arch),
              command.Command([
                  PnaclTool('ar'), 'rc',
                  command.path.join(clang_libdir, 'libpnaclmm.a'),
                  'pnaclmm.bc']),
          ]
      }
    })
  else:
    # For now most of the D2N support libs currently come from our native
    # translator libs (libgcc_eh, compiler_rt, crtbegin/end). crti/crtn and crt1
    # come from libnacl, built by scons/gyp.  TODO(dschuff): Do proper support
    # libs for D2N which also don't use .init/.fini

    # Translate from bias_arch's triple-style (i686) names to the translator's
    # style (x86-32). We don't change the translator's naming scheme to avoid
    # churning the in-browser translator.
    def TL(lib):
      return GSDJoin(lib, pynacl.platform.GetArch3264(bias_arch))
    def TranslatorFile(lib, filename):
      return os.path.join('%(' + TL(lib) + ')s', filename)
    libs.update({
      T('libs_support'): {
          'type': TargetLibBuildType(is_canonical),
          'dependencies': [ GSDJoin('newlib', MultilibArch(bias_arch)),
                            TL('compiler_rt'), TL('libgcc_eh'),
                            'target_lib_compiler'],
          'inputs': { 'src': os.path.join(NACL_DIR, 'pnacl', 'support')},
          'commands': [
              command.Mkdir(clang_libdir, parents=True),
              command.Copy(
                  TranslatorFile('compiler_rt', 'libgcc.a'),
                  os.path.join('%(output)s', clang_libdir, 'libgcc.a')),
              command.Copy(
                  TranslatorFile('libgcc_eh', 'libgcc_eh.a'),
                  os.path.join('%(output)s', clang_libdir, 'libgcc_eh.a')),
              BuildTargetObjectCmd('clang_direct/crtbegin.c', 'crtbeginT.o',
                                   bias_arch, output_dir=clang_libdir),
              BuildTargetObjectCmd('crtend.c', 'crtend.o',
                                   bias_arch, output_dir=clang_libdir),
          ],
      }
    })
    # We do not plan to support libstdcxx for direct-to-nacl since we are close
    # to being able to remove it from PNaCl.
    del libs[T('libstdcxx')]

  return libs


def TranslatorLibs(arch, is_canonical):
  setjmp_arch = arch
  if setjmp_arch.endswith('-nonsfi'):
    setjmp_arch = setjmp_arch[:-len('-nonsfi')]

  arch_cmds = []
  if arch == 'arm':
    arch_cmds.append(
        BuildTargetTranslatorCmd('aeabi_read_tp.S', 'aeabi_read_tp.o', arch))
  elif arch == 'x86-32-nonsfi':
    arch_cmds.extend(
        [BuildTargetTranslatorCmd('entry_linux.c', 'entry_linux.o', arch),
         BuildTargetTranslatorCmd('entry_linux_x86_32.S', 'entry_linux_asm.o',
                                  arch)])
  elif arch == 'arm-nonsfi':
    arch_cmds.extend(
        [BuildTargetTranslatorCmd('entry_linux.c', 'entry_linux.o', arch),
         BuildTargetTranslatorCmd('entry_linux_arm.S', 'entry_linux_asm.o',
                                  arch)])
  translator_lib_dir = os.path.join('translator', arch, 'lib')

  libs = {
      GSDJoin('libs_support_translator', arch): {
          'type': TargetLibBuildType(is_canonical),
          'output_subdir': translator_lib_dir,
          'dependencies': [ 'newlib_src', 'newlib_le32',
                            'target_lib_compiler'],
          # These libs include
          # arbitrary stuff from native_client/src/{include,untrusted,trusted}
          'inputs': { 'src': os.path.join(NACL_DIR, 'pnacl', 'support'),
                      'include': os.path.join(NACL_DIR, 'src'),
                      'newlib_subset': os.path.join(
                          NACL_DIR, 'src', 'third_party',
                          'pnacl_native_newlib_subset')},
          'commands': [
              BuildTargetTranslatorCmd('crtbegin.c', 'crtbegin.o', arch,
                                       output_dir='%(output)s'),
              BuildTargetTranslatorCmd('crtbegin.c', 'crtbegin_for_eh.o', arch,
                                       ['-DLINKING_WITH_LIBGCC_EH'],
                                       output_dir='%(output)s'),
              BuildTargetTranslatorCmd('crtend.c', 'crtend.o', arch,
                                       output_dir='%(output)s'),
              # libcrt_platform.a
              BuildTargetTranslatorCmd('pnacl_irt.c', 'pnacl_irt.o', arch),
              BuildTargetTranslatorCmd('relocate.c', 'relocate.o', arch),
              BuildTargetTranslatorCmd(
                  'setjmp_%s.S' % setjmp_arch.replace('-', '_'),
                  'setjmp.o', arch),
              BuildTargetTranslatorCmd('string.c', 'string.o', arch,
                                       ['-std=c99'],
                                       source_dir='%(newlib_subset)s'),
              # Pull in non-errno __ieee754_fmod from newlib and rename it to
              # fmod. This is to support the LLVM frem instruction.
              BuildTargetTranslatorCmd(
                  'e_fmod.c', 'e_fmod.o', arch,
                  ['-std=c99', '-I%(abs_newlib_src)s/newlib/libm/common/',
                   '-D__ieee754_fmod=fmod'],
                  source_dir='%(abs_newlib_src)s/newlib/libm/math'),
              BuildTargetTranslatorCmd(
                  'ef_fmod.c', 'ef_fmod.o', arch,
                  ['-std=c99', '-I%(abs_newlib_src)s/newlib/libm/common/',
                   '-D__ieee754_fmodf=fmodf'],
                  source_dir='%(abs_newlib_src)s/newlib/libm/math')] +
              arch_cmds + [
              command.Command(' '.join([
                  PnaclTool('ar'), 'rc',
                  command.path.join('%(output)s', 'libcrt_platform.a'),
                  '*.o']), shell=True),
              # Dummy IRT shim
              BuildTargetTranslatorCmd(
                  'dummy_shim_entry.c', 'dummy_shim_entry.o', arch),
              command.Command([PnaclTool('ar'), 'rc',
                               command.path.join('%(output)s',
                                                 'libpnacl_irt_shim_dummy.a'),
                               'dummy_shim_entry.o']),
          ],
      },
      GSDJoin('compiler_rt', arch): {
          'type': TargetLibBuildType(is_canonical),
          'output_subdir': translator_lib_dir,
          'dependencies': ['compiler_rt_src', 'target_lib_compiler',
                           'newlib_le32'],
          'commands': [
              command.Command(MakeCommand() + [
                  '-f',
                  command.path.join('%(compiler_rt_src)s', 'lib', 'builtins',
                                    'Makefile-pnacl'),
                  'libgcc.a', 'CC=' + PnaclTool('clang'),
                  'AR=' + PnaclTool('ar')] +
                  ['SRC_DIR=' + command.path.join('%(abs_compiler_rt_src)s',
                                                  'lib', 'builtins'),
                   'CFLAGS=-arch ' + arch + ' -DPNACL_' +
                    arch.replace('-', '_') + ' --pnacl-allow-translate -O3 ' +
                   NewlibIsystemCflags('le32')]),
              command.Copy('libgcc.a', os.path.join('%(output)s', 'libgcc.a')),
          ],
      },
  }
  if not arch.endswith('-nonsfi'):
    libs.update({
      GSDJoin('libgcc_eh', arch): {
          'type': TargetLibBuildType(is_canonical),
          'output_subdir': translator_lib_dir,
          'dependencies': [ 'gcc_src', 'target_lib_compiler'],
          'inputs': { 'scripts': os.path.join(NACL_DIR, 'pnacl', 'scripts')},
          'commands': [
              # Instead of trying to use gcc's build system to build only
              # libgcc_eh, we just build the C files and archive them manually.
              command.RemoveDirectory('include'),
              command.Mkdir('include'),
              command.Copy(os.path.join('%(gcc_src)s', 'gcc',
                           'unwind-generic.h'),
                           os.path.join('include', 'unwind.h')),
              command.Copy(os.path.join('%(scripts)s', 'libgcc-tconfig.h'),
                           'tconfig.h'),
              command.WriteData('', 'tm.h'),
              BuildLibgccEhCmd('unwind-dw2.c', 'unwind-dw2.o', arch),
              BuildLibgccEhCmd('unwind-dw2-fde-glibc.c',
                               'unwind-dw2-fde-glibc.o', arch),
              command.Command([PnaclTool('ar'), 'rc',
                               command.path.join('%(output)s', 'libgcc_eh.a'),
                               'unwind-dw2.o', 'unwind-dw2-fde-glibc.o']),
          ],
      },
    })
  return libs

def UnsandboxedIRT(arch, is_canonical):
  libs = {
      GSDJoin('unsandboxed_irt', arch): {
          'type': TargetLibBuildType(is_canonical),
          'output_subdir': os.path.join('translator', arch, 'lib'),
          # This lib #includes
          # arbitrary stuff from native_client/src/{include,untrusted,trusted}
          'inputs': { 'support': os.path.join(NACL_DIR, 'src', 'nonsfi', 'irt'),
                      'untrusted': os.path.join(
                          NACL_DIR, 'src', 'untrusted', 'irt'),
                      'include': os.path.join(NACL_DIR, 'src'), },
          'commands': [
              # The NaCl headers insist on having a platform macro such as
              # NACL_LINUX defined, but src/nonsfi/irt_interfaces.c does not
              # itself use any of these macros, so defining NACL_LINUX here
              # even on non-Linux systems is OK.
              # TODO(dschuff): this include path breaks the input encapsulation
              # for build rules.
              command.Command([
                  'gcc', '-m32', '-O2', '-Wall', '-Werror',
                  '-I%(top_srcdir)s/..', '-DNACL_LINUX=1', '-DDEFINE_MAIN',
                  '-c', command.path.join('%(support)s', 'irt_interfaces.c'),
                  '-o', command.path.join('%(output)s', 'unsandboxed_irt.o')]),
              command.Command([
                  'gcc', '-m32', '-O2', '-Wall', '-Werror',
                  '-I%(top_srcdir)s/..',
                  '-c', command.path.join('%(support)s', 'irt_random.c'),
                  '-o', command.path.join('%(output)s', 'irt_random.o')]),
              command.Command([
                  'gcc', '-m32', '-O2', '-Wall', '-Werror',
                  '-I%(top_srcdir)s/..',
                  '-c', command.path.join('%(untrusted)s', 'irt_query_list.c'),
                  '-o', command.path.join('%(output)s', 'irt_query_list.o')]),
          ],
      },
  }
  return libs


def SDKCompiler(arches):
  arch_packages = ([GSDJoin('newlib', arch) for arch in arches] +
                   [GSDJoin('libcxx', arch) for arch in arches])
  compiler = {
      'sdk_compiler': {
          'type': 'work',
          'output_subdir': 'sdk_compiler',
          'dependencies': ['target_lib_compiler'] + arch_packages,
          'commands': [
              command.CopyRecursive('%(' + t + ')s', '%(output)s')
              for t in ['target_lib_compiler'] + arch_packages],
      },
  }
  return compiler


def SDKLibs(arch, is_canonical):
  scons_flags = ['--verbose', 'MODE=nacl', '-j%(cores)s', 'naclsdk_validate=0',
                 'pnacl_newlib_dir=%(abs_sdk_compiler)s',
                 'DESTINATION_ROOT=%(work_dir)s']
  if arch == 'le32':
    scons_flags.extend(['bitcode=1', 'platform=x86-32'])
  elif not IsBCArch(arch):
    scons_flags.extend(['nacl_clang=1',
                        'platform=' + pynacl.platform.GetArch3264(arch)])
  else:
    raise ValueError('Should not be building SDK libs for', arch)
  libs = {
      GSDJoin('core_sdk_libs', arch): {
          'type': TargetLibBuildType(is_canonical),
          'dependencies': ['sdk_compiler', 'target_lib_compiler'],
          'inputs': {
              'src_untrusted': os.path.join(NACL_DIR, 'src', 'untrusted'),
              'src_include': os.path.join(NACL_DIR, 'src', 'include'),
              'scons.py': os.path.join(NACL_DIR, 'scons.py'),
              'site_scons': os.path.join(NACL_DIR, 'site_scons'),
              'prep_nacl_sdk':
                  os.path.join(NACL_DIR, 'build', 'prep_nacl_sdk.py'),
          },
          'commands': [
            command.Command(
                [sys.executable, '%(scons.py)s',
                 'includedir=' +os.path.join('%(output)s',
                                             TripleFromArch(MultilibArch(arch)),
                                             'include'),
                 'libdir=' + os.path.join('%(output)s', MultilibLibDir(arch)),
                 'install'] + scons_flags,
                cwd=NACL_DIR),
          ],
      }
  }
  return libs
