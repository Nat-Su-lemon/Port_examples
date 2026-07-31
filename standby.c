#define BKP_PW_LEN_REG   0            // register index holding length
#define BKP_PW_DATA_REG  1            // first data register

void store_wifi_password(const char *pw)
{
    uint32_t len = strlen(pw);
    if (len > 63) len = 63;

    HAL_PWR_EnableBkUpAccess();

    HAL_RTCEx_BKUPWrite(&hrtc, BKP_PW_LEN_REG, len);

    uint32_t word = 0;
    uint32_t reg  = BKP_PW_DATA_REG;
    for (uint32_t i = 0; i < len; i++) {
        word |= (uint32_t)(uint8_t)pw[i] << ((i & 3) * 8);
        if ((i & 3) == 3) {
            HAL_RTCEx_BKUPWrite(&hrtc, reg++, word);
            word = 0;
        }
    }
    if (len & 3)                          // flush partial word
        HAL_RTCEx_BKUPWrite(&hrtc, reg, word);
}

uint32_t load_wifi_password(char *out /* >=64 bytes */)
{
    uint32_t len = HAL_RTCEx_BKUPRead(&hrtc, BKP_PW_LEN_REG);
    if (len > 63) { out[0] = '\0'; return 0; }   // uninitialized / garbage guard

    uint32_t reg = BKP_PW_DATA_REG;
    for (uint32_t i = 0; i < len; i++) {
        if ((i & 3) == 0)
            /* refetch word at each 4-byte boundary */;
        uint32_t word = HAL_RTCEx_BKUPRead(&hrtc, reg + (i >> 2));
        out[i] = (char)((word >> ((i & 3) * 8)) & 0xFF);
    }
    out[len] = '\0';
    return len;
}

// wake handling

if (__HAL_PWR_GET_FLAG(PWR_FLAG_SBF)) {   // woke from Standby
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SBF);
    char pw[64];
    load_wifi_password(pw);
    // ... use pw
}

/*
The key idea is a single dashboard_data_t struct that acts as the contract between "whoever produces data" 
(your sensor tasks, WiFi, fuel gauge) and "whoever draws it" (this GUI module). Producers write fields and 
flip a per-field ready bit; the GUI reads fields whose ready bit is set and renders them. A done flag lets 
the GUI signal back that a full refresh finished — useful right before you drop into Stop 2.
*/
