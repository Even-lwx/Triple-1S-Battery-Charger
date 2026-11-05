"""Execute real compiled Thumb board/app code with HAL test doubles.
Requires pip install unicorn pyelftools, and build_gcc.py output.
This tests firmware logic, not STM32 peripherals or the physical board.
"""
import pathlib
import struct
import unittest
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

ELF = pathlib.Path(__file__).resolve().parents[1] / '_reports/gcc/charger.elf'

class Firmware:
    def __init__(self):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        for base, size in [(0x08000000,0x10000),(0x20000000,0x2000),(0x1fff7000,0x1000),
                           (0x40020000,0x10000),(0x50000000,0x2000)]:
            self.uc.mem_map(base,size)
        self.uc.mem_write(0x08000000,b'\xff'*0x10000)
        with ELF.open('rb') as stream:
            elf = ELFFile(stream)
            self.symbols = {s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
            for section in elf.iter_sections():
                if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS' and section.data():
                    self.uc.mem_write(section['sh_addr'], section.data())
        self.uc.mem_write(0x1fff75aa,struct.pack('<H',1500))
        self.values = [1364,914,372,870,186,900,93,1984,2048]
        self.index = -1
        self.fail_poll = False
        self.stops = 0
        self.writes = []
        self.configured = []
        self.tick = 0
        self.hooks = {}
        self.programs=0; self.erases=[]; self.fail_program=0
        self.stub('Board_Log',lambda:0)
        self.stub('HAL_Delay',lambda:0)
        self.stub('HAL_GetTick',lambda:self.tick)
        self.stub('HAL_ADC_ConfigChannel',self.configure_adc)
        self.stub('HAL_ADC_Start',lambda:0)
        self.stub('HAL_ADC_PollForConversion',lambda:3 if self.fail_poll else 0)
        self.stub('HAL_ADC_GetValue',lambda:self.values[self.index])
        self.stub('HAL_ADC_Stop',self.stop_adc)
        self.stub('HAL_GPIO_Init',self.gpio_init)
        self.stub('HAL_GPIO_WritePin',self.gpio_write)
        self.stub('HAL_FLASH_Unlock',lambda:0)
        self.stub('HAL_FLASH_Lock',lambda:0)
        self.stub('HAL_FLASH_GetError',lambda:0)
        self.stub('HAL_FLASH_Program',self.flash_program)
        self.stub('HAL_FLASHEx_Erase',self.flash_erase)
        self.uc.hook_add(UC_HOOK_CODE,self.dispatch)
    def reg(self, name): return self.uc.reg_read(name)
    def stub(self,name,fn):
        self.hooks[self.symbols[name] & ~1] = fn
    def dispatch(self,uc,address,size,data):
        if address in self.hooks:
            value = self.hooks[address]()
            uc.reg_write(UC_ARM_REG_R0, value or 0)
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    def configure_adc(self):
        self.index += 1
        self.configured.append(struct.unpack('<III',bytes(self.uc.mem_read(self.reg(UC_ARM_REG_R1),12))))
        return 0
    def stop_adc(self): self.stops += 1; return 0
    def gpio_write(self):
        self.writes.append((self.reg(UC_ARM_REG_R0),self.reg(UC_ARM_REG_R1),self.reg(UC_ARM_REG_R2)))
    def flash_program(self):
        self.programs+=1
        if self.fail_program==self.programs:return 1
        address=self.reg(UC_ARM_REG_R1)
        assert 0x0800f000<=address<=0x0800fff8 and address%8==0
        assert bytes(self.uc.mem_read(address,8))==b'\xff'*8
        self.uc.mem_write(address,struct.pack('<II',self.reg(UC_ARM_REG_R2),self.reg(UC_ARM_REG_R3)))
        return 0
    def flash_erase(self):
        kind,bank,page,count=struct.unpack('<IIII',bytes(self.uc.mem_read(self.reg(UC_ARM_REG_R0),16)))
        assert page in (30,31) and count==1
        self.erases.append(page)
        self.uc.mem_write(0x08000000+page*2048,b'\xff'*2048)
        self.uc.mem_write(self.reg(UC_ARM_REG_R1),struct.pack('<I',0xffffffff))
        return 0
    def gpio_init(self):
        port = self.reg(UC_ARM_REG_R0)
        pins,mode,pull = struct.unpack('<III',bytes(self.uc.mem_read(self.reg(UC_ARM_REG_R1),12)))
        assert pull == 0, 'ISET2 must not use pullups/pulldowns'
        moder = struct.unpack('<I',bytes(self.uc.mem_read(port,4)))[0]
        for bit in range(16):
            if pins & (1<<bit): moder = (moder & ~(3<<(2*bit))) | ((mode&3)<<(2*bit))
        self.uc.mem_write(port,struct.pack('<I',moder))
    def call(self,name,*args):
        for reg,value in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2],args):
            self.uc.reg_write(reg,value & 0xffffffff)
        self.uc.reg_write(UC_ARM_REG_SP,0x20001ff0)
        self.uc.reg_write(UC_ARM_REG_LR,0x0800fff1)
        self.uc.emu_start(self.symbols[name] | 1,0x0800fff0,count=2000000)
        assert self.reg(UC_ARM_REG_PC) == 0x0800fff0, 'Function failed to return'
        return self.reg(UC_ARM_REG_R0)

class BoardTests(unittest.TestCase):
    def test_flash_restore_unchanged_and_failed_commit(self):
        f=Firmware();f.call('Board_GPIO_Init')
        self.assertEqual(f.call('Settings_Restore'),0)
        self.assertEqual([f.call('Board_CurrentMA',i) for i in range(3)],[100]*3)
        self.assertEqual(f.call('Settings_SetCurrent',0,2),0)
        self.assertEqual(f.call('Settings_SetCurrent',1,1),0)
        self.assertEqual(f.programs,4)
        self.assertEqual(f.call('Settings_SetCurrent',1,1),0)
        self.assertEqual(f.programs,4) # 值未变化时不写入
        f.fail_program=6 # 数据写入成功，但提交失败
        self.assertEqual(f.call('Settings_SetCurrent',2,2),1)
        self.assertEqual(f.call('Board_CurrentMA',2),100)
        image=bytes(f.uc.mem_read(0x0800f000,4096))
        reboot=Firmware();reboot.uc.mem_write(0x0800f000,image)
        reboot.call('Board_GPIO_Init');reboot.call('Settings_Restore')
        self.assertEqual([reboot.call('Board_CurrentMA',i) for i in range(3)],[1000,500,100])
        self.assertEqual(reboot.call('Settings_SetCurrent',2,2),0)
        self.assertEqual(reboot.call('Settings_All100'),0)
        reboot.call('Settings_Restore')
        self.assertEqual([reboot.call('Board_CurrentMA',i) for i in range(3)],[100]*3)
    def test_flash_rollover_preserves_old_page_and_rejects_corruption(self):
        import zlib
        f=Firmware()
        for i in range(128):
            payload=0x43530102
            first=struct.pack('<II',i+1,payload)
            f.uc.mem_write(0x0800f000+i*16,first+struct.pack('<II',zlib.crc32(first),0x434f4d31))
        f.call('Settings_Restore')
        self.assertEqual(f.call('Board_CurrentMA',0),1000)
        old=bytes(f.uc.mem_read(0x0800f000,2048))
        f.fail_program=2
        self.assertEqual(f.call('Settings_SetCurrent',0,1),1)
        self.assertEqual(f.erases,[31])
        self.assertEqual(bytes(f.uc.mem_read(0x0800f000,2048)),old)
        f.call('Settings_Restore');self.assertEqual(f.call('Board_CurrentMA',0),1000)
        f.fail_program=0
        self.assertEqual(f.call('Settings_SetCurrent',0,1),0)
        f.call('Settings_Restore');self.assertEqual(f.call('Board_CurrentMA',0),500)
        # 破坏最新记录的 CRC 后，上一条完整记录仍可使用。
        f.uc.mem_write(0x0800f808,b'\x00'*4)
        f.call('Settings_Restore');self.assertEqual(f.call('Board_CurrentMA',0),1000)
    def test_dividers_gain_reference_and_channel_selection(self):
        f=Firmware(); out=0x20001800
        self.assertEqual(f.call('Board_Sample',out),0)
        raw=struct.unpack('<8H8IB3x',bytes(f.uc.mem_read(out,52)))
        self.assertEqual(raw[-1],1)
        vdda,vin,*rest=raw[8:-1]
        self.assertAlmostEqual(vdda,3300,delta=3)
        self.assertAlmostEqual(vin,5000,delta=8)
        self.assertAlmostEqual(rest[0],4200,delta=8)
        self.assertAlmostEqual(rest[3],100,delta=2)
        self.assertEqual(len(f.configured),9)
        # 必须在序位 1 替换通道，不得在扫描掩码中累加通道。
        self.assertEqual(len(set(c[0] for c in f.configured)),9)
        self.assertEqual(len(set(c[1] for c in f.configured)),1)
        self.assertEqual(f.stops,81)
    def test_timeout_invalidates_previous_data_and_stops_adc(self):
        f=Firmware();out=0x20001800;f.uc.mem_write(out+48,b'\x01');f.fail_poll=True
        self.assertNotEqual(f.call('Board_Sample',out),0)
        self.assertEqual(bytes(f.uc.mem_read(out+48,1)),b'\x00')
        self.assertEqual(f.stops,1)
    def test_reference_and_current_saturation_rejected(self):
        for index,value in [(0,0),(0,400),(2,4095)]:
            f=Firmware();f.values[index]=value
            self.assertNotEqual(f.call('Board_Sample',0x20001800),0)
    def test_gpio_keys_and_three_current_states(self):
        f=Firmware()
        self.assertEqual(f.call('Board_GPIO_Init'),0)
        for channel in range(3):
            for mode,ma in [(1,500),(2,1000),(0,100)]:
                f.writes.clear()
                self.assertEqual(f.call('Board_SetCurrent',channel,mode),0)
                self.assertEqual(f.call('Board_CurrentMA',channel),ma)
                if mode == 0: self.assertEqual(f.writes,[], 'float transition must not drive low')
                else: self.assertEqual(f.writes,[(0x50000400,1<<(10+channel),1 if mode==1 else 0)])
        f.writes.clear()
        self.assertEqual(f.call('Board_SetCurrent',3,0),1)
        self.assertEqual(f.call('Board_SetCurrent',0,-1),1)
        self.assertEqual(f.writes,[])
    def test_no_false_charge_disable(self):
        f=Firmware();f.writes.clear()
        self.assertEqual(f.call('Board_SetChargeEnabled',0,0),2)
        self.assertEqual(f.writes,[])
    def test_uart_unavailable_preserves_error_count(self):
        f=Firmware(); del f.hooks[f.symbols['Board_Log'] & ~1]
        f.uc.mem_write(0x20001800,b'TEST\x00')
        self.assertEqual(f.call('Board_Log',0x20001800),1)
        fault=f.symbols['board_fault']
        self.assertEqual(struct.unpack('<III',bytes(f.uc.mem_read(fault,12)))[2],1)
    def test_menu_debounce_and_explicit_current_apply(self):
        f=Firmware()
        f.stub('Board_ADC_Init',lambda:0)
        f.stub('Board_Sample',lambda:1)
        f.stub('OLED_Init',lambda:0)
        f.stub('OLED_Update',lambda:0)
        def tick(time, pressed):
            f.tick=time
            f.uc.mem_write(0x50000410,struct.pack('<I',0xffff & ~(pressed<<4)))
            f.call('App_Tick')
        f.uc.mem_write(0x50000410,struct.pack('<I',0xffff))
        f.call('Board_GPIO_Init'); f.call('App_Init')
        tick(1600,0)
        tick(1610,4);tick(1639,4)
        self.assertEqual([f.call('Board_CurrentMA',i) for i in range(3)],[100]*3)
        tick(1640,4);tick(1800,4) # 详情界面，持续按住按键不能进入菜单
        self.assertEqual(f.uc.mem_read(f.symbols['page'],1),b'\x01')
        time=1900
        def press(key):
            nonlocal time
            tick(time,0);tick(time+30,0);tick(time+40,key);tick(time+70,key);time+=100
        press(4) # 菜单
        press(4) # 电流编辑界面
        press(2) # 待应用值为 500，尚未生效
        self.assertEqual(f.call('Board_CurrentMA',0),100)
        press(8) # 取消
        self.assertEqual(f.call('Board_CurrentMA',0),100)
        press(4);press(2);press(4) # 重新打开，选择 500 并应用
        self.assertEqual(f.call('Board_CurrentMA',0),500)
        press(4);press(2);press(4) # 菜单中将所有通道设为 100 并应用
        self.assertEqual([f.call('Board_CurrentMA',i) for i in range(3)],[100]*3)
    def test_voltage_proxy_bounds_and_monotonicity(self):
        f=Firmware()
        def percent(mv):
            n=f.call('UI_VoltagePercent',mv)
            return n if n<0x80000000 else n-0x100000000
        self.assertEqual(percent(0),-1)
        self.assertEqual(percent(4400),-1)
        self.assertEqual(percent(4200),99)
        values=[percent(mv) for mv in range(2500,4251,25)]
        self.assertEqual(values,sorted(values))
        self.assertLessEqual(max(values),99)
    def test_led_empty_charging_full_estimate_and_invalid(self):
        f=Firmware();out=0x20001800
        f.call('Board_GPIO_Init');f.call('ChargeLED_Init')
        self.assertEqual([(w[0],w[1]) for w in f.writes[-3:]],
                         [(0x50000800,1<<13),(0x50000400,1<<8),(0x50000400,1<<9)])
        def sample(time,ma,mv=4180,valid=1):
            f.uc.mem_write(out,struct.pack('<8H8IB3x',*([0]*8),3300,5000,mv,mv,mv,ma,ma,ma,valid))
            f.call('ChargeLED_Sample',out,time)
        def lights(time):
            f.writes.clear();f.call('ChargeLED_Tick',time)
            return [w[2] for w in f.writes]
        sample(0,1);self.assertEqual(lights(0),[1]*3)
        sample(1000,100);sample(3000,100)
        self.assertEqual(lights(3000),[0]*3)
        self.assertEqual(lights(3500),[1]*3)
        sample(4000,9);sample(33000,9)
        self.assertEqual(lights(33500),[1]*3) # 尚未达到 30 秒
        sample(34000,9);self.assertEqual(lights(34500),[0]*3)
        # 空插座判断优先于先前的满电估计，不能保持满电状态。
        sample(35000,1);sample(38000,1)
        self.assertEqual(lights(38000),[1]*3)
        # 重新插入 4.2 V、低电流的电池时，不能立即宣称满电。
        sample(40000,5);sample(80000,5)
        self.assertEqual(lights(80000),[1]*3)
        sample(81000,100);sample(83000,100)
        sample(84000,100,valid=0)
        self.assertEqual(lights(84000),[1]*3)
    def test_oled_transfer_failure_always_releases_cs(self):
        f=Firmware();calls=[]
        def spi():
            calls.append(bytes(f.uc.mem_read(f.reg(UC_ARM_REG_R1),f.reg(UC_ARM_REG_R2))))
            return 3 if len(calls)==2 else 0
        f.stub('HAL_SPI_Transmit',spi)
        self.assertEqual(f.call('OLED_Update'),3)
        self.assertEqual(calls[0],b'\xb0\x00\x10')
        self.assertEqual(len(calls[1]),128)
        self.assertEqual(f.writes[-1],(0x50000c00,8,1))

if __name__ == '__main__': unittest.main(verbosity=2)
