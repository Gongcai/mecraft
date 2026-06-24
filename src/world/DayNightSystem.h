 #ifndef MECRAFT_DAY_NIGHT_SYSTEM_H
#define MECRAFT_DAY_NIGHT_SYSTEM_H

class DayNightSystem {
public:
    DayNightSystem();

    // Update with delta time in seconds
    void update(float dt);

    // Set time manually (0.0 to 1200.0)
    void setTimeOfDay(float time);

    // Get current time of day in seconds [0.0, 1200.0)
    float getTimeOfDay() const;

    // Get total elapsed in-game time in seconds
    double getTotalGameTime() const;

    // Get the number of passed days
    int getElapsedDays() const;

    // Get the sun/sky light intensity multiplier [0.0, 1.0]
    float getSkyIntensity() const;

    // Return whether the current sky curve is dark enough for bed sleep.
    bool isNightTime() const;

    // Return whether the current sky curve has reached full daytime brightness.
    bool isFullDaytime() const;

    // Get current day progress in [0.0, 1.0)
    float getDayProgress01() const;

    // Get the celestial rotation angle in radians for sun/moon placement.
    float getCelestialAngleRadians() const;

    // Get moon phase index in [0, 7].
    int getMoonPhaseIndex() const;

private:
    double m_totalGameTime;
    float m_timeOfDay;
    int m_elapsedDays;
    
    float m_skyIntensity;

    // A whole day is 20 minutes = 1200 seconds
    static constexpr float SECONDS_PER_DAY = 1200.0f;
};

#endif // MECRAFT_DAY_NIGHT_SYSTEM_H
