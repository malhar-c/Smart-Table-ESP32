struct Standing_Desk : Service::WindowCovering{

    SpanCharacteristic *targetPosition;
    SpanCharacteristic *currentPosition;
    SpanCharacteristic *positionState;
    SpanCharacteristic *obstacleDetected;

    long sitMemorySteps   = -1;
    long standMemorySteps = -1;

    unsigned long lastNvsSave             = 0;
    unsigned long lastOledUpdate          = 0;
    unsigned long lastActivityTime        = 0;
    unsigned long bothHeldSince           = 0;
    unsigned long obstructionDisplayUntil = 0;

    bool homingActive = false;

    bool displayAsleep = false;
    // Tracks whether each memory button was pressed while the display was asleep,
    // so the first press only wakes the screen without executing the desk action.
    bool pressedWhileAsleep[BTN_COUNT] = {};

    Standing_Desk() : Service::WindowCovering()
    {
        targetPosition   = new Characteristic::TargetPosition(0, true);
        currentPosition  = new Characteristic::CurrentPosition(0, true);
        positionState    = new Characteristic::PositionState(2);
        obstacleDetected = new Characteristic::ObstructionDetected(false);

        prefs.begin(NVS_NAMESPACE, true);
        long savedSteps   = clampSteps(prefs.getLong(NVS_KEY_POS,   0));
        sitMemorySteps    = prefs.getLong(NVS_KEY_SIT,   -1);
        standMemorySteps  = prefs.getLong(NVS_KEY_STAND, -1);
        prefs.end();

        Serial.printf("[DESK] NVS load: pos=%ld  sit=%ld  stand=%ld\n",
            savedSteps, sitMemorySteps, standMemorySteps);

        stepper->setCurrentPosition(savedSteps);
        stepsTarget = savedSteps;

        int posPercent = (int)map(savedSteps, 0, STEPPER_MAX_STEPS, 0, 100);
        currentPosition->setVal(posPercent);
        targetPosition->setVal(posPercent);

        lastActivityTime = millis();
    }

    boolean update() override
    {
        obstacleDetected->setVal(false);
        stepsTarget = clampSteps(map(targetPosition->getNewVal(), 0, 100, 0, STEPPER_MAX_STEPS));
        resetActivity();   // HomeKit command counts as activity
        applySlackCompensation(stepsTarget);
        stepper->moveTo(stepsTarget);
        return true;
    }

    void loop() override
    {
        long currentSteps = stepper->getCurrentPosition();
        int  currentPct   = (int)map(currentSteps, 0, STEPPER_MAX_STEPS, 0, 100);

        // Desk moving (incl. via HomeKit) wakes and keeps the display on
        if (stepper->isRunning()) { resetActivity(); if (displayAsleep) wakeDisplay(); }

        handleLimitSwitch(currentSteps);
        handleObstacle(currentPct);
        handleUpDown(currentSteps);
        handleSitStand(currentSteps);
        updatePositionState(currentPct);
        periodicNvsSave(currentSteps);
        checkDisplaySleep();
        updateOled(currentSteps);

        if (currentPosition->timeVal() > 500) {
            currentPosition->setVal(currentPct);
        }
    }

private:

    void resetActivity()
    {
        lastActivityTime = millis();
    }

    void wakeDisplay()
    {
        if (displayAsleep) {
            oled.ssd1306_command(SSD1306_DISPLAYON);
            displayAsleep = false;
        }
    }

    void sleepDisplay()
    {
        if (!displayAsleep) {
            oled.clearDisplay();
            oled.display();
            oled.ssd1306_command(SSD1306_DISPLAYOFF);
            displayAsleep = true;
        }
    }

    void checkDisplaySleep()
    {
        if (!displayAsleep && (millis() - lastActivityTime > DISPLAY_SLEEP_MS)) {
            sleepDisplay();
        }
    }

    void showOledMessage(const char *msg)
    {
        wakeDisplay();
        oled.clearDisplay();
        oled.setFont(NULL);          // must reset — custom font would render off-screen
        oled.setTextSize(2);
        oled.setTextColor(SSD1306_WHITE);
        oled.setCursor(20, 22);
        oled.print(msg);
        oled.display();
        delay(900);
        resetActivity();
    }

    void applySlackCompensation(long newTarget)
    {
        long currentSteps = stepper->getCurrentPosition();
        bool newDir = directionSign(newTarget - currentSteps);

        if (newDir != previousDirection && stepper->isRunning()) {
            stepper->stopMove();
            while (stepper->isRunning()) {}

            stepper->setSpeedInHz(SLACK_SPEED_HZ);
            stepper->setAcceleration(SLACK_ACCEL);
            stepper->move(newDir ? SLACK_STEPS : -SLACK_STEPS);
            while (stepper->isRunning()) {}

            stepper->setSpeedInHz(STEPPER_SPEED_HZ);
            stepper->setAcceleration(STEPPER_ACCEL);
        }
        previousDirection = newDir;
    }

    void handleLimitSwitch(long &currentSteps)
    {
        if (!limitSwitchTriggered) return;
        Serial.println("[DESK] Limit switch triggered — homing to 0");

        stepper->forceStop();
        delay(50);
        while (stepper->isRunning()) {}

        stepper->setCurrentPosition(0);
        stepsTarget  = 0;
        currentSteps = 0;

        currentPosition->setVal(0);
        targetPosition->setVal(0);
        savePosition(0);
        limitSwitchTriggered = false;
        homingActive = false;

        showOledMessage("HOME");
    }

    void handleObstacle(int currentPct)
    {
        if (!obstacleTriggered) return;
        Serial.printf("[DESK] Obstacle triggered at %d%%\n", currentPct);

        stepper->forceStop();
        obstacleDetected->setVal(true);
        targetPosition->setVal(currentPct);
        stepsTarget    = stepper->getCurrentPosition();
        vibrationCount = 0;
        obstacleTriggered = false;

        wakeDisplay();
        resetActivity();
        obstructionDisplayUntil = millis() + 3000;
        delay(500);
    }

    void triggerHoming()
    {
        homingActive   = true;
        bothHeldSince  = 0;
        stepsTarget    = 0;
        vibrationCount = 0;
        Serial.println("[DESK] Auto-homing triggered — moving to limit switch");
        stepper->moveTo(0);
    }

    void handleUpDown(long currentSteps)
    {
        bool upHeld   = btnIsHeld(BTN_UP,   BTN_IDX_UP);
        bool downHeld = btnIsHeld(BTN_DOWN, BTN_IDX_DOWN);

        // Log button transitions (not every loop tick — only on change)
        static bool prevUp = false, prevDown = false;
        if (upHeld != prevUp || downHeld != prevDown) {
            Serial.printf("[BTN] UP=%d DOWN=%d  pos=%ld  running=%d  asleep=%d\n",
                upHeld, downHeld, currentSteps, stepper->isRunning(), displayAsleep);
            prevUp = upHeld; prevDown = downHeld;
        }

        // ── Both held: homing gesture ──────────────────────────────────────
        if (upHeld && downHeld) {
            if (displayAsleep) { wakeDisplay(); resetActivity(); bothHeldSince = 0; return; }
            resetActivity();
            if (bothHeldSince == 0) bothHeldSince = millis();
            if (!homingActive && (millis() - bothHeldSince >= HOMING_HOLD_MS)) {
                triggerHoming();
            }
            return; // suppress normal movement while both held
        }

        // Reset countdown if released before threshold
        if (bothHeldSince != 0 && !homingActive) bothHeldSince = 0;

        // While homing: don't accept normal button input
        if (homingActive) return;

        // ── Normal single-button logic ─────────────────────────────────────
        if (upHeld || downHeld) {
            if (displayAsleep) {
                Serial.println("[BTN] Display was asleep — waking, ignoring this press");
                wakeDisplay();
                resetActivity();
                return;
            }
            resetActivity();
        }

        if (upHeld && !downHeld) {
            if (!stepper->isRunning() || !directionSign(stepsTarget - currentSteps)) {
                stepsTarget = STEPPER_MAX_STEPS;
                Serial.printf("[BTN] UP → moveTo %ld (from %ld)\n", stepsTarget, currentSteps);
                applySlackCompensation(stepsTarget);
                stepper->moveTo(stepsTarget);
            }
        } else if (downHeld && !upHeld) {
            if (!stepper->isRunning() || directionSign(stepsTarget - currentSteps)) {
                stepsTarget = 0;
                Serial.printf("[BTN] DOWN → moveTo %ld (from %ld)\n", stepsTarget, currentSteps);
                applySlackCompensation(stepsTarget);
                stepper->moveTo(stepsTarget);
            }
        } else if (!upHeld && !downHeld) {
            if (stepper->isRunning() && (stepsTarget == 0 || stepsTarget == STEPPER_MAX_STEPS)) {
                stepper->stopMove();
                stepsTarget = stepper->getCurrentPosition();
                Serial.printf("[BTN] Released — stopMove at %ld\n", stepsTarget);
                targetPosition->setVal((int)map(stepsTarget, 0, STEPPER_MAX_STEPS, 0, 100));
            }
        }

        if (digitalRead(ENABLE_PIN)) {
            vibrationCount = 0;
        } else {
            previousDirection = digitalRead(stepper->getDirectionPin());
        }
    }

    void handleSitStand(long currentSteps)
    {
        // Detect new press events before btnJustReleased updates state, so we can
        // record whether the display was asleep at the moment of the press.
        for (int i = BTN_IDX_SIT; i <= BTN_IDX_STAND; i++) {
            int pin = (i == BTN_IDX_SIT) ? BTN_SIT : BTN_STAND;
            bool nowHeld = !digitalRead(pin);
            if (!currentBtnState[i] && nowHeld) {
                // Falling edge: button just pressed
                pressedWhileAsleep[i] = displayAsleep;
                resetActivity();
                if (displayAsleep) wakeDisplay();
            }
        }

        // Sit button
        if (btnJustReleased(BTN_SIT, BTN_IDX_SIT)) {
            if (!pressedWhileAsleep[BTN_IDX_SIT]) {
                unsigned long held = millis() - btnPressTime[BTN_IDX_SIT];
                if (held >= LONG_PRESS_MS) {
                    sitMemorySteps = currentSteps;
                    prefs.begin(NVS_NAMESPACE, false);
                    prefs.putLong(NVS_KEY_SIT, sitMemorySteps);
                    prefs.end();
                    showOledMessage("SAVED!");
                } else if (sitMemorySteps >= 0) {
                    stepsTarget = sitMemorySteps;
                    applySlackCompensation(stepsTarget);
                    stepper->moveTo(stepsTarget);
                    targetPosition->setVal((int)map(stepsTarget, 0, STEPPER_MAX_STEPS, 0, 100));
                }
            }
            pressedWhileAsleep[BTN_IDX_SIT] = false;
        }

        // Stand button
        if (btnJustReleased(BTN_STAND, BTN_IDX_STAND)) {
            if (!pressedWhileAsleep[BTN_IDX_STAND]) {
                unsigned long held = millis() - btnPressTime[BTN_IDX_STAND];
                if (held >= LONG_PRESS_MS) {
                    standMemorySteps = currentSteps;
                    prefs.begin(NVS_NAMESPACE, false);
                    prefs.putLong(NVS_KEY_STAND, standMemorySteps);
                    prefs.end();
                    showOledMessage("SAVED!");
                } else if (standMemorySteps >= 0) {
                    stepsTarget = standMemorySteps;
                    applySlackCompensation(stepsTarget);
                    stepper->moveTo(stepsTarget);
                    targetPosition->setVal((int)map(stepsTarget, 0, STEPPER_MAX_STEPS, 0, 100));
                }
            }
            pressedWhileAsleep[BTN_IDX_STAND] = false;
        }
    }

    void updatePositionState(int currentPct)
    {
        int targetPct = targetPosition->getVal();
        int state;
        if (!stepper->isRunning())       state = 2;
        else if (currentPct < targetPct) state = 1;
        else                             state = 0;
        positionState->setVal(state);
    }

    void periodicNvsSave(long currentSteps)
    {
        if (!stepper->isRunning() && (millis() - lastNvsSave > 2000)) {
            savePosition(currentSteps);
            lastNvsSave = millis();
        }
    }

    void drawArrow(bool up)
    {
        // Filled triangle arrow, bottom-right corner
        if (up) {
            oled.fillTriangle(106, 63, 117, 49, 128, 63, SSD1306_WHITE);
        } else {
            oled.fillTriangle(106, 49, 117, 63, 128, 49, SSD1306_WHITE);
        }
    }

    // Draw one 7-segment digit. Segment bits: a=0,b=1,c=2,d=3,e=4,f=5,g=6
    void draw7Seg(int x, int y, int w, int h, int t, uint8_t m)
    {
        int midY = y + (h - t) / 2;
        if (m & 0x01) oled.fillRect(x,         y,         w,     t,     SSD1306_WHITE); // a
        if (m & 0x02) oled.fillRect(x + w - t, y,         t,     h / 2, SSD1306_WHITE); // b
        if (m & 0x04) oled.fillRect(x + w - t, y + h / 2, t,     h / 2, SSD1306_WHITE); // c
        if (m & 0x08) oled.fillRect(x,         y + h - t, w,     t,     SSD1306_WHITE); // d
        if (m & 0x10) oled.fillRect(x,         y + h / 2, t,     h / 2, SSD1306_WHITE); // e
        if (m & 0x20) oled.fillRect(x,         y,         t,     h / 2, SSD1306_WHITE); // f
        if (m & 0x40) oled.fillRect(x,         midY,      w,     t,     SSD1306_WHITE); // g
    }

    void drawHeight7Seg(float height)
    {
        static const uint8_t segMap[10] =
            {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};

        char buf[8];
        sprintf(buf, "%.1f", height);

        const int w = 18, h = 40, t = 4, gap = 5, dotSize = 5, dotGap = 4, y = 6;

        // Measure for horizontal centering
        int total = 0;
        for (char *p = buf; *p; p++)
            total += (*p == '.') ? (dotSize + dotGap) : (w + gap);
        total -= gap;

        int x = (OLED_WIDTH - total) / 2;
        if (x < 0) x = 0;

        for (char *p = buf; *p; p++) {
            if (*p == '.') {
                oled.fillRect(x, y + h - dotSize, dotSize, dotSize, SSD1306_WHITE);
                x += dotSize + dotGap;
            } else {
                draw7Seg(x, y, w, h, t, segMap[*p - '0']);
                x += w + gap;
            }
        }
    }

    void updateOled(long currentSteps)
    {
        if (displayAsleep) return;
        if (millis() - lastOledUpdate < 200) return;
        lastOledUpdate = millis();

        oled.clearDisplay();
        oled.setFont(NULL);
        oled.setTextSize(1);   // custom GFX fonts MUST stay at size 1; reset each frame
        oled.setTextColor(SSD1306_WHITE);

        // ── Homing countdown ──────────────────────────────────────────────
        if (bothHeldSince != 0 && !homingActive) {
            int pct = (int)((millis() - bothHeldSince) * 100 / HOMING_HOLD_MS);
            if (pct > 100) pct = 100;
            oled.setTextSize(2);
            oled.setCursor(10, 8);
            oled.print("HOME?");
            oled.drawRect(4, 36, 120, 14, SSD1306_WHITE);
            oled.fillRect(4, 36, pct * 120 / 100, 14, SSD1306_WHITE);
            oled.display();
            return;
        }

        // ── Homing in progress ────────────────────────────────────────────
        if (homingActive) {
            oled.setTextSize(2);
            oled.setCursor(10, 8);
            oled.print("HOMING");
            drawArrow(false);
            oled.display();
            return;
        }

        // ── Save countdown (Sit or Stand held) ───────────────────────────
        bool sitHeld   = currentBtnState[BTN_IDX_SIT]   && !pressedWhileAsleep[BTN_IDX_SIT];
        bool standHeld = currentBtnState[BTN_IDX_STAND] && !pressedWhileAsleep[BTN_IDX_STAND];
        if (sitHeld || standHeld) {
            int idx = sitHeld ? BTN_IDX_SIT : BTN_IDX_STAND;
            int pct = (int)((millis() - btnPressTime[idx]) * 100 / LONG_PRESS_MS);
            if (pct > 100) pct = 100;
            oled.setTextSize(2);
            oled.setCursor(10, 8);
            oled.print(sitHeld ? "SIT?" : "STD?");
            oled.drawRect(4, 36, 120, 14, SSD1306_WHITE);
            oled.fillRect(4, 36, pct * 120 / 100, 14, SSD1306_WHITE);
            oled.display();
            return;
        }

        // ── Obstruction warning ───────────────────────────────────────────
        if (millis() < obstructionDisplayUntil) {
            // Blink the border so it reads as an alert
            bool on = (millis() / 400) % 2;
            if (on) {
                oled.drawRect(0, 0, OLED_WIDTH, OLED_HEIGHT, SSD1306_WHITE);
                oled.drawRect(1, 1, OLED_WIDTH - 2, OLED_HEIGHT - 2, SSD1306_WHITE);
            }
            oled.setTextSize(2);
            oled.setCursor(22, 14);
            oled.print("DESK");
            oled.setCursor(4, 36);
            oled.print("BLOCKED");
            oled.display();
            return;
        }

        // ── Normal display — 7-segment height ─────────────────────────────
        drawHeight7Seg(stepsToHeight(currentSteps));

        // Filled triangle arrow at bottom-right while moving
        if (stepper->isRunning()) {
            drawArrow(directionSign(stepsTarget - currentSteps));
        }

        oled.display();
    }
};
