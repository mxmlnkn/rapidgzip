"""
Distutils doesn't support nasm, so this is a custom compiler for NASM
"""

from distutils.unixccompiler import UnixCCompiler
from distutils.sysconfig import get_config_var
import platform
import sys


class NasmCompiler(UnixCCompiler) :
    compiler_type = 'nasm'
    src_extensions = ['.asm']
    obj_extension = '.obj'
    language_map = {".asm"   : "asm",}
    language_order = ["asm"]
    executables = {'preprocessor' : None,
               'compiler'     : ["nasm"],
               'compiler_so'  : ["nasm"],
               'compiler_cxx' : ["nasm"],
               'linker_so'    : ["cc", "-shared"],
               'linker_exe'   : ["cc", "-shared"],
               'archiver'     : ["ar", "-cr"],
               'ranlib'       : None,
               }
    def __init__ (self,
                  verbose=0,
                  dry_run=0,
                  force=0):

        UnixCCompiler.__init__ (self, verbose, dry_run, force)
        self.set_executable("compiler", "nasm")

    def _is_gcc(self, compiler_name):
        return False  # ¯\_(ツ)_/¯

    def _get_cc_args(self, pp_opts, debug, before):
        if sys.platform == 'darwin':
            # Fix the symbols on macOS
            cc_args = pp_opts + ["-f macho64","-DNOPIE","--prefix=_"]
        else:
            # Use ELF format for Linux. Else, we would get this error:
            #   error: binary format does not support any special symbol types
            cc_args = pp_opts + ["-f elf64" if platform.machine().endswith('64') else "-f elf32"]
        if debug:
            # Debug symbols from NASM
            cc_args[:0] = ['-g']
        if before:
            cc_args[:0] = before
        return cc_args


    def _compile(self, obj, src, ext, cc_args, extra_postargs, pp_opts):
        # The implementation in UnixCCompiler calls compiler_fixup here.
        # But it is not needed for NASM and actually leads to build errors on MacOS
        # because it adds a non-NASM option "-arch ...".
        try:
            self.spawn(self.compiler_so + cc_args + [src, '-o', obj] + extra_postargs)
        except DistutilsExecError as msg:
            raise CompileError(msg)

    def link(self, target_desc, objects,
             output_filename, output_dir=None, libraries=None,
             library_dirs=None, runtime_library_dirs=None,
             export_symbols=None, debug=0, extra_preargs=None,
             extra_postargs=None, build_temp=None, target_lang=None):
        # Make sure libpython gets linked
        if not self.runtime_library_dirs:
            self.runtime_library_dirs.append(get_config_var('LIBDIR'))
        if not self.libraries:
            libraries = ["python" + get_config_var("LDVERSION")]
        if not extra_preargs:
            extra_preargs = []

        return super().link(target_desc, objects,
                            output_filename, output_dir, libraries,
                            library_dirs, runtime_library_dirs,
                            export_symbols, debug, extra_preargs,
                            extra_postargs, build_temp, target_lang)

    def runtime_library_dir_option(self, dir):
        if sys.platform == "darwin":
            return "-L" + dir
        else:
            return "-Wl,-R" + dir
