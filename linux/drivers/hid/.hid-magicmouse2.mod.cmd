cmd_/home/alex/Projects/magicmouse-hid/linux/drivers/hid/hid-magicmouse2.mod := printf '%s\n'   hid-magicmouse2.o | awk '!x[$$0]++ { print("/home/alex/Projects/magicmouse-hid/linux/drivers/hid/"$$0) }' > /home/alex/Projects/magicmouse-hid/linux/drivers/hid/hid-magicmouse2.mod
