#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif


static const char ____versions[]
__used __section("__versions") =
	"\x14\x00\x00\x00\x56\x6b\x3c\xf5"
	"_dev_err\0\0\0\0"
	"\x20\x00\x00\x00\x85\x58\x3c\xcd"
	"hid_unregister_driver\0\0\0"
	"\x1c\x00\x00\x00\x26\x49\x34\x33"
	"input_mt_init_slots\0"
	"\x20\x00\x00\x00\x95\x55\xfe\xd3"
	"input_set_abs_params\0\0\0\0"
	"\x1c\x00\x00\x00\x28\x46\x9a\x51"
	"input_alloc_absinfo\0"
	"\x2c\x00\x00\x00\xc6\xfa\xb1\x54"
	"__ubsan_handle_load_invalid_value\0\0\0"
	"\x10\x00\x00\x00\xa6\x50\xba\x15"
	"jiffies\0"
	"\x24\x00\x00\x00\x31\x93\xbe\x0e"
	"input_mt_get_slot_by_key\0\0\0\0"
	"\x14\x00\x00\x00\x74\xf8\x41\x1d"
	"input_event\0"
	"\x24\x00\x00\x00\x33\x79\xf0\xe2"
	"input_mt_report_slot_state\0\0"
	"\x28\x00\x00\x00\xb3\x1c\xa2\x87"
	"__ubsan_handle_out_of_bounds\0\0\0\0"
	"\x1c\x00\x00\x00\x56\x76\xc3\x68"
	"input_mt_sync_frame\0"
	"\x2c\x00\x00\x00\x0b\xb8\x00\x30"
	"input_mt_report_pointer_emulation\0\0\0"
	"\x14\x00\x00\x00\x4f\x6f\x76\xda"
	"_dev_warn\0\0\0"
	"\x18\x00\x00\x00\xfe\xfc\x9c\x12"
	"devm_kmalloc\0\0\0\0"
	"\x20\x00\x00\x00\x6a\xdf\xee\xff"
	"delayed_work_timer_fn\0\0\0"
	"\x18\x00\x00\x00\x39\x63\xf4\xc6"
	"init_timer_key\0\0"
	"\x18\x00\x00\x00\xd8\x4f\xa1\x3e"
	"hid_open_report\0"
	"\x18\x00\x00\x00\x64\xbd\xfd\x87"
	"hid_hw_start\0\0\0\0"
	"\x1c\x00\x00\x00\x24\xe9\x4c\x31"
	"hid_register_report\0"
	"\x14\x00\x00\x00\xd3\x85\x33\x2d"
	"system_wq\0\0\0"
	"\x20\x00\x00\x00\x6d\xb5\xfc\xb2"
	"queue_delayed_work_on\0\0\0"
	"\x18\x00\x00\x00\xb7\x7e\xac\x94"
	"param_ops_bool\0\0"
	"\x18\x00\x00\x00\xb2\xe2\x9e\x66"
	"param_get_uint\0\0"
	"\x14\x00\x00\x00\xbb\x6d\xfb\xbd"
	"__fentry__\0\0"
	"\x14\x00\x00\x00\x87\x73\x3c\x5c"
	"kstrtoull\0\0\0"
	"\x1c\x00\x00\x00\xca\x39\x82\x5b"
	"__x86_return_thunk\0\0"
	"\x1c\x00\x00\x00\x56\xb9\x19\x0a"
	"__stack_chk_fail\0\0\0\0"
	"\x20\x00\x00\x00\x2a\x24\xa2\xc1"
	"__hid_register_driver\0\0\0"
	"\x24\x00\x00\x00\x4a\x18\xa7\x9f"
	"cancel_delayed_work_sync\0\0\0\0"
	"\x14\x00\x00\x00\x3e\xeb\xdf\x28"
	"hid_hw_stop\0"
	"\x10\x00\x00\x00\x41\xcc\xb4\x66"
	"kmemdup\0"
	"\x1c\x00\x00\x00\x03\xb4\x8b\xc6"
	"hid_hw_raw_request\0\0"
	"\x10\x00\x00\x00\xba\x0c\x7a\x03"
	"kfree\0\0\0"
	"\x18\x00\x00\x00\xc1\x35\x7c\x15"
	"module_layout\0\0\0"
	"\x00\x00\x00\x00\x00\x00\x00\x00";

MODULE_INFO(depends, "hid");

MODULE_ALIAS("hid:b0005g*v000005ACp0000030D");
MODULE_ALIAS("hid:b0005g*v0000004Cp00000269");
MODULE_ALIAS("hid:b0005g*v000005ACp0000030E");
MODULE_ALIAS("hid:b0005g*v0000004Cp00000265");
MODULE_ALIAS("hid:b0003g*v000005ACp00000265");

MODULE_INFO(srcversion, "0E998A699FF9D50F6C7AA9D");
