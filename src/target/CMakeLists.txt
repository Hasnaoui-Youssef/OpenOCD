openocd_add_object_library(target_core
    algorithm.c
    register.c
    image.c
    breakpoints.c
    target.c
    target_request.c
    testee.c
    semihosting_common.c
    smp.c
    rtt.c
)
target_link_libraries(target_core PUBLIC jim)

openocd_add_object_library(target_arm_debug
    arm_dpm.c
    arm_jtag.c
    arm_disassembler.c
    arm_simulator.c
    arm_semihosting.c
    arm_adi_v5.c
    arm_dap.c
    armv7a_cache.c
    armv7a_cache_l2x.c
    adi_v5_dapdirect.c
    adi_v5_jtag.c
    adi_v5_swd.c
    embeddedice.c
    trace.c
    etb.c
    etm.c
    etm_dummy.c
    arm_tpiu_swo.c
    arm_cti.c
    arm_tmc.c
    arm_etmv4.c
)
if(OPENOCD_HAVE_CAPSTONE)
    target_link_libraries(target_arm_debug PUBLIC OpenOCD::capstone)
endif()

openocd_add_object_library(target_arm7_9
    arm7_9_common.c
    arm7tdmi.c
    arm720t.c
    arm9tdmi.c
    arm920t.c
    arm966e.c
    arm946e.c
    arm926ejs.c
    feroceon.c
)

openocd_add_object_library(target_armv4_5
    armv4_5.c
    armv4_5_mmu.c
    armv4_5_cache.c
)

openocd_add_object_library(target_arm_misc
    fa526.c
    xscale.c
)

openocd_add_object_library(target_armv6
    arm11.c
    arm11_dbgtap.c
)

openocd_add_object_library(target_armv7
    armv7m.c
    armv7m_cache.c
    armv7m_trace.c
    cortex_m.c
    armv7a.c
    armv7a_mmu.c
    cortex_a.c
    ls1_sap.c
    mem_ap.c
)

openocd_add_object_library(target_armv8
    armv8_dpm.c
    armv8_opcodes.c
    aarch64.c
    a64_disassembler.c
    armv8.c
    armv8_cache.c
)
if(OPENOCD_HAVE_CAPSTONE)
    target_link_libraries(target_armv8 PUBLIC OpenOCD::capstone)
endif()

openocd_add_object_library(target_avr32
    avr32_ap7k.c
    avr32_jtag.c
    avr32_mem.c
    avr32_regs.c
)

openocd_add_object_library(target_mips32
    mips32.c
    mips_m4k.c
    mips32_pracc.c
    mips32_dmaacc.c
    mips_ejtag.c
)

openocd_add_object_library(target_mips64
    mips64.c
    mips64_pracc.c
    mips_mips64.c
)

openocd_add_object_library(target_stm8
    stm8.c
)

openocd_add_object_library(target_intel_ia32
    quark_x10xx.c
    quark_d20xx.c
    lakemont.c
    x86_32_common.c
)

openocd_add_object_library(target_esirisc
    esirisc.c
    esirisc_jtag.c
    esirisc_trace.c
)

openocd_add_object_library(target_arc
    arc.c
    arc_cmd.c
    arc_jtag.c
    arc_mem.c
)

openocd_add_object_library(target_misc
    avrt.c
    dsp563xx.c
    dsp563xx_once.c
    dsp5680xx.c
    hla_target.c
)

add_subdirectory(openrisc)
add_subdirectory(riscv)
add_subdirectory(xtensa)
add_subdirectory(espressif)
