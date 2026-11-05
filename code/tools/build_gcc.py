"""Build the same C source list as Keil using STM32 GCC. No source generation.
Usage: python code/tools/build_gcc.py --compiler <arm-none-eabi-gcc.exe>
"""
import argparse
import pathlib
import subprocess
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('--compiler', required=True)
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
output = root / '_reports' / 'gcc'
output.mkdir(parents=True, exist_ok=True)
compiler = pathlib.Path(args.compiler)
project = ET.parse(root / 'MDK-ARM/code.uvprojx')
includes = project.find('.//Cads/VariousControls/IncludePath').text.split(';')
common = ['-mcpu=cortex-m0plus', '-mthumb', '-Os', '-g3', '-ffunction-sections', '-fdata-sections',
          '-DUSE_HAL_DRIVER', '-DSTM32G031xx']
for include in includes:
    common += ['-I', str((root / 'MDK-ARM' / include).resolve())]
objects = []
for index, item in enumerate(project.findall('.//Groups/Group/Files/File')):
    source = (root / 'MDK-ARM' / item.findtext('FilePath')).resolve()
    if source.suffix.lower() != '.c':
        continue
    obj = output / (str(index) + '_' + source.stem + '.o')
    subprocess.run([str(compiler), *common, '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-c', str(source), '-o', str(obj)], check=True)
    objects.append(str(obj))
startup = root / 'Drivers/CMSIS/Device/ST/STM32G0xx/Source/Templates/gcc/startup_stm32g031xx.s'
obj = output / 'startup.o'
subprocess.run([str(compiler), *common, '-c', str(startup), '-o', str(obj)], check=True)
objects.append(str(obj))
elf = output / 'charger.elf'
subprocess.run([str(compiler), *common, *objects, '-T', str(root/'tools/stm32g031.ld'),
                '--specs=nano.specs', '--specs=nosys.specs', '-Wl,--gc-sections',
                '-Wl,--undefined=Board_SetChargeEnabled', '-Wl,-Map='+str(output/'charger.map'), '-o', str(elf)], check=True)
for fmt in ('ihex', 'binary'):
    suffix = 'hex' if fmt == 'ihex' else 'bin'
    subprocess.run([str(compiler.with_name('arm-none-eabi-objcopy.exe')), '-O', fmt,
                    str(elf), str(output/('charger.'+suffix))], check=True)
subprocess.run([str(compiler.with_name('arm-none-eabi-size.exe')), str(elf)], check=True)
print('Built:', elf)
