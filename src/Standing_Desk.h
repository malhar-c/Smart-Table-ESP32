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
    unsigned long homingStartTime         = 0;

    bool homingActive   = false;
    bool homingReleased = false;  // true once both buttons fully released after homing triggered

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
        bool newDir = directionSign(newTarget - stepper->getCurrentPosition());
        // On direction reversal while running: stop immediately.
        // Blocking waits removed — they caused infinite hangs when the vibration ISR
        // disabled the motor mid-burst (ENABLE_PIN HIGH), leaving isRunning() stuck true.
        if (newDir != previousDirection && stepper->isRunning()) {
            stepper->forceStop();
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
        homingActive    = false;
        homingStartTime = 0;

        showOledMessage("HOME");
    }

    void handleObstacle(int currentPct)
    {
        if (!obstacleTriggered) return;
        Serial.printf("[DESK] Obstacle triggered at %d%%  homing=%d\n", currentPct, homingActive);

        stepper->forceStop();
        obstacleDetected->setVal(true);
        targetPosition->setVal(currentPct);
        stepsTarget    = stepper->getCurrentPosition();
        vibrationCount = 0;
        obstacleTriggered = false;
        homingActive    = false;   // abort homing if obstacle hit during homing sequence
        homingStartTime = 0;

        wakeDisplay();
        resetActivity();
        obstructionDisplayUntil = millis() + 3000;
    }

    void triggerHoming()
    {
        homingActive    = true;
        homingReleased  = false;
        bothHeldSince   = 0;
        homingStartTime = millis();
        stepsTarget     = 0;
        vibrationCount  = 0;
        Serial.println("[DESK] Auto-homing triggered — moving to limit switch");
        stepper->moveTo(0);
    }

    void handleUpDown(long currentSteps)
    {
        bool upHeld   = btnIsHeld(BTN_UP,   BTN_IDX_UP);
        bool downHeld = btnIsHeld(BTN_DOWN, BTN_IDX_DOWN);

        // Any button press wakes display first, regardless of firmware state
        if ((upHeld || downHeld) && displayAsleep) {
            wakeDisplay();
            resetActivity();
            return;
        }

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

        // While homing: track release, allow manual abort, and auto-abort if motor stalls
        if (homingActive) {
            if (!upHeld && !downHeld) {
                // Both fully released — safe to allow single-button abort now
                homingReleased = true;
            }
            if (homingReleased && (upHeld != downHeld)) {
                // Fresh single-button press after release — user wants to abort
                homingActive    = false;
                homingReleased  = false;
                homingStartTime = 0;
                stepper->forceStop();
                stepsTarget = currentSteps;
                Serial.println("[DESK] Homing aborted by button press");
                resetActivity();
            } else if (!stepper->isRunning() &&
                       homingStartTime != 0 &&
                       millis() - homingStartTime > 1500) {
                // Motor never started (already at home, stuck, etc.) — auto-abort
                homingActive    = false;
                homingReleased  = false;
                homingStartTime = 0;
                stepsTarget     = currentSteps;
                Serial.println("[DESK] Homing auto-aborted — motor not running");
                resetActivity();
            }
            return;
        }

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
        // Small filled triangle, top-left corner
        if (up) {
            oled.fillTriangle(2, 14, 10, 2, 18, 14, SSD1306_WHITE);
        } else {
            oled.fillTriangle(2, 2, 10, 14, 18, 2, SSD1306_WHITE);
        }
    }

    // 7-segment digit. H = position of middle bar; vH = height of each vertical segment.
    // Both halves are perfectly symmetric — no segment overlaps or overruns.
    void draw7Seg(int x, int y, int w, int h, int t, uint8_t m)
    {
        const int H  = (h - t) / 2;   // offset from y to top of middle bar
        const int vH = H - t;          // vertical segment height (same top & bottom)
        if (m & 0x01) oled.fillRect(x + t,     y,         w - 2*t, t,  SSD1306_WHITE); // a top
        if (m & 0x02) oled.fillRect(x + w - t, y + t,     t,       vH, SSD1306_WHITE); // b top-R
        if (m & 0x04) oled.fillRect(x + w - t, y + H + t, t,       vH, SSD1306_WHITE); // c bot-R
        if (m & 0x08) oled.fillRect(x + t,     y + h - t, w - 2*t, t,  SSD1306_WHITE); // d bottom
        if (m & 0x10) oled.fillRect(x,         y + H + t, t,       vH, SSD1306_WHITE); // e bot-L
        if (m & 0x20) oled.fillRect(x,         y + t,     t,       vH, SSD1306_WHITE); // f top-L
        if (m & 0x40) oled.fillRect(x + t,     y + H,     w - 2*t, t,  SSD1306_WHITE); // g middle
    }

    void drawHeight7Seg(float height)
    {
        static const uint8_t segMap[10] =
            {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};

        char buf[8];
        sprintf(buf, "%.1f", height);

        const int w = 20, h = 56, t = 4, gap = 4;
        const int dotW = 6, dotH = 6, dotGap = 3;
        const int y = 4, rightEdge = 124;

        // Measure total drawn width (no trailing gap after last char)
        int total = 0;
        for (char *p = buf; *p; p++)
            total += (*p == '.') ? (dotW + dotGap) : (w + gap);
        total -= gap;

        int x = rightEdge - total;
        if (x < 22) x = 22; // clamp — don't overlap the arrow

        for (char *p = buf; *p; p++) {
            if (*p == '.') {
                oled.fillRect(x, y + h - dotH, dotW, dotH, SSD1306_WHITE);
                x += dotW + dotGap;
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
            static const char *dotFrames[] = {"   ", ".  ", ".. ", "..."};
            int frame = (millis() / 400) % 4;

            oled.setTextSize(2);
            oled.setCursor(28, 4);     // centered: (128 - 6*12) / 2
            oled.print("HOMING");
            oled.setCursor(46, 28);    // centered: (128 - 3*12) / 2
            oled.print(dotFrames[frame]);

            oled.setTextSize(1);
            char buf[16];
            sprintf(buf, "%.1f cm", stepsToHeight(currentSteps));
            oled.setCursor((128 - (int)strlen(buf) * 6) / 2, 52);
            oled.print(buf);

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
            const char *label = sitHeld ? "SAVE SIT" : "SAVE STAND";
            oled.setTextSize(2);
            oled.setCursor((128 - (int)strlen(label) * 12) / 2, 8);
            oled.print(label);
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
            oled.setCursor(40, 14);   // (128 - 4*12) / 2 = 40
            oled.print("DESK");
            oled.setCursor(22, 36);   // (128 - 7*12) / 2 = 22
            oled.print("BLOCKED");
            oled.display();
            return;
        }

        // ── Normal display — 7-segment height ─────────────────────────────
        drawHeight7Seg(stepsToHeight(currentSteps));

        // Filled triangle arrow at bottom-right while moving
        if (stepper->isRunning()) {
            drawArrow(previousDirection);
        }

        oled.display();
    }
};
