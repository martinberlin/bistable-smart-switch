# Make sure to setup your I2C GPIOs in menuconfig -> DS3231 Configuration
set(srcs 
    # RTC
    "ds3231/main/ds3231.c"

    # NXP RTC
    "pcf8563/main/pcf8563.c"

    # Touch controller Focal Tech
    "ft6x36/FT6X36.cpp"
    # Shared between both
    "i2cdev.c"
    # PCF8563 used by Lilygo S3 parallel EPD047
    #"pcf8563/main/pcf8563.c"
)

idf_component_register(SRCS ${srcs}
    REQUIRES driver esp_timer
	INCLUDE_DIRS "ds3231/main"
                 "ft6x36/include"
                 "pcf8563/main"
		         "include"
)
