#!/bin/bash

FIRMWARE=$1

GPIO_RESET="EC RESET"
GPIO_BOOT0="EC SWCLK/BOOT0"

BOOTLOADER_I2C_ADDR=0x56

DT_OVERLAY_NAME=wbec_fw_overlay
DT_OVERLAY_DIR=/sys/kernel/config/device-tree/overlays/$DT_OVERLAY_NAME
DT_OVERLAY="
/dts-v1/;
/plugin/;
/ {
    compatible = \"wirenboard,wirenboard-740\";

    fragment-wbec-disable {
        target = <&wbec>;
        __overlay__ {
            status = \"disabled\";
        };
    };

    fragment-vin-disable {
        target = <&vin>;
        __overlay__ {
            status = \"disabled\";
        };
    };

    fragment-spi-disable {
        target = <&spi2>;
        __overlay__ {
            status = \"disabled\";
        };
    };

    fragment-i2c-fw {
        target = <&i2c_wbec>;
        __overlay__ {
            status = \"okay\";
            #address-cells = <1>;
            #size-cells = <0>;
        };
    };
};
"

remove_overlay_and_start_drivers() {
    rmdir $DT_OVERLAY_DIR

    modprobe pwrkey-wbec

    systemctl restart wb-mqtt-gpio
    systemctl restart wb-mqtt-adc
    systemctl restart watchdog
}

# Check firmware file exists
if [ ! -f "$FIRMWARE" ]; then
    echo "Firmware file not found"
    exit -1
fi

# Try to find GPIO
GPIO_RESET_NUM=$(gpiofind "$GPIO_RESET") || {
    echo "GPIO $GPIO_RESET not found"
    exit -1
}
GPIO_BOOT0_NUM=$(gpiofind "$GPIO_BOOT0") || {
    echo "GPIO $GPIO_BOOT0 not found"
    exit -1
}

# Remove pwrkey driver
modprobe -r pwrkey-wbec

# Stop services
systemctl stop wb-mqtt-gpio
systemctl stop wb-mqtt-adc
systemctl stop watchdog


# Apply overlay to disable spi and enable i2c
rmdir $DT_OVERLAY_DIR 2> /dev/null
mkdir $DT_OVERLAY_DIR
echo "$DT_OVERLAY" | dtc -Idts -Odtb > $DT_OVERLAY_DIR/dtbo
sleep 0.5

# Find i2c bus with name i2c_wbec
I2C_BUS=/dev/$(i2cdetect -l | grep i2c_wbec | cut -f 1 -d$'\t') || {
    echo "I2C bus not found"
    remove_overlay_and_start_drivers
    exit -1
}
echo "I2C bus: $I2C_BUS"


# Reset to bootloader
gpioset $GPIO_BOOT0_NUM=1
sleep 0.1
gpioset $GPIO_RESET_NUM=1
sleep 0.1
gpioset $GPIO_RESET_NUM=0
sleep 0.5
gpioset $GPIO_BOOT0_NUM=0

# Check that bootloader is running
stm32flash $I2C_BUS -a$BOOTLOADER_I2C_ADDR || {
    echo "Bootloader not found"
    remove_overlay_and_start_drivers
    exit -1
}

# Write option bytes for stay in bootloader forever
stm32flash -w - -S 0x1FFF7800:4 -a$BOOTLOADER_I2C_ADDR $I2C_BUS < <(echo -n -e \\xAA\\xE1\\xFF\\xDB) || {
    echo "Can't write option bytes before firmware"
    remove_overlay_and_start_drivers
    exit -1
}
# Update firmware
stm32flash -w $FIRMWARE -a$BOOTLOADER_I2C_ADDR $I2C_BUS || {
    echo "Can't write firmware"
    remove_overlay_and_start_drivers
    exit -1
}
# Restore option bytes, go to main app
stm32flash -w - -S 0x1FFF7800:4 -a$BOOTLOADER_I2C_ADDR $I2C_BUS < <(echo -n -e \\xAA\\xE1\\xFF\\xDA) || {
    echo "Can't write option bytes after firmware"
    remove_overlay_and_start_drivers
    exit -1
}

# For start firmware
sleep 1.0

remove_overlay_and_start_drivers
