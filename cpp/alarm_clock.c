#include <limits.h>
#include "alarm_clock.h"

void maybe_update_min_value_ms(unsigned long *min_value_ms, unsigned int min_value_sec);

unsigned int idt_alarm(const struct alarm_config *alarmConfig, const unsigned int now_sec);

unsigned int p88n_alarm(const struct alarm_config *alarmConfig, const unsigned int now_sec);

unsigned int time_quota_alarm(const struct alarm_config *alarmConfig, const unsigned int now_sec);

unsigned int dy9x_alarm(const struct alarm_config *alarmConfig, const unsigned int now_sec);

unsigned int monitoring_time_alarm(const struct alarm_config *alarmConfig, const unsigned int now_sec);

unsigned int zb12_alarm(const struct alarm_config *alarmConfig, const unsigned int now_sec);

void clear_zb12_flag_if_set(const struct alarm_config *alarmConfig);

// Added seconds to milliseconds conversion function
unsigned int s_to_ms(unsigned int seconds) {
    return seconds * 1000;
}

// Added min function
unsigned long min(unsigned long a, unsigned long b) {
    if(a < b)
        return a;
    return b;
}

// Note: functions are semantically pointers, so omitting 'Pointer' in type name
// E.g, 'puts' is a pointer to a function, and 'puts("hello world")' calls that
// function. So does pts("hello world") if pts is declared to be a pointer to a function
typedef unsigned int (*AlarmFunction)(const struct alarm_config*, const unsigned int);

void
how_long_until_the_next_alarm(struct alarm_config *alarmConfig,
                              const unsigned int now_sec,
                              unsigned long *min_value_ms) {

    // I cleaned this up slightly by removing the hard-coded "6" in array size,
    // and using array initializer instead.
    AlarmFunction alarm_functions[] = {
        idt_alarm,
        p88n_alarm,
        time_quota_alarm,
        zb12_alarm,
        dy9x_alarm,
        monitoring_time_alarm, // comma on last elements Python-style :)
    };

    // How many alarms are the? Python style CAPITAL_CONSTANT_NAME
    unsigned int const NUM_FUNCTIONS =
        sizeof(alarm_functions) / sizeof(alarm_functions[0]);
    
    // I added "_s" to smallest to clarify unit
    // Local convention: _s (variable unit is seconds)
    //                   _ms (variable unit is milliseconds)
    unsigned int smallest_s = INT_MAX;
    for (int i = 0; i < NUM_FUNCTIONS; ++i) {
        unsigned int alarm_time_s = alarm_functions[i](alarmConfig, now_sec);
        // Using minimization function for more readability
        smallest_s = min(alarm_time_s, smallest_s);
    }

    clear_zb12_flag_if_set(alarmConfig);

    // I refactored this to just be a minimize function.
    // This is because INT_MAX is 'local knowledge' in this
    // function, and was spread out into the maybe_update_min_value_ms.
    // I think this is easier to grokk.
    if(smallest_s != INT_MAX) {
        *min_value_ms = min(s_to_ms(smallest_s), *min_value_ms);
    }
}

void clear_zb12_flag_if_set(const struct alarm_config *alarmConfig) {
    unsigned int reporting_flags = get_reporting_flags(alarmConfig);
    if (reporting_flags & ZJ77_REPORTING_TRIGGERS_ZB12) {
        if ((get_quota_holding_time(alarmConfig) != 0) &&
            !get_operational_flag_state(alarmConfig, OPERATIONAL_FLAG_ZB12_STOPPED)) {

            if (get_operational_flag_state(alarmConfig, OPERATIONAL_FLAG_ZB12_MODIFIED))
                add_operational_flag(alarmConfig, OPERATIONAL_FLAG_ZB12_MODIFIED);
        }
    }
}

unsigned int zb12_alarm(const struct alarm_config *alarmConfig, const unsigned int now_sec) {
    unsigned int time_sec = INT_MAX;
    unsigned int lreporting_flags = get_reporting_flags(alarmConfig);
    if (lreporting_flags & ZJ77_REPORTING_TRIGGERS_ZB12) {
        if ((get_quota_holding_time(alarmConfig) != 0) &&
            !get_operational_flag_state(alarmConfig, OPERATIONAL_FLAG_ZB12_STOPPED)) {
            /* If ZB12 is just provisioned, start timer with provisioned value
             * If ZB12 is modified, start timer with modified value and reset ZB12 modified flag
             * If ZB12 is not modified (and not just provisioned), restart the session timer for remainder of the provisioned value
             */
            time_sec = get_quota_holding_time(alarmConfig);

            if (!get_operational_flag_state(alarmConfig, OPERATIONAL_FLAG_ZB12_MODIFIED) &&
                (get_time_of_last_pkt(alarmConfig) != 0)) {
                unsigned int last_pkt_diff = now_sec - get_time_of_last_pkt(alarmConfig);
                time_sec -= last_pkt_diff;
            }
        }
    }
    return time_sec;
}

unsigned int monitoring_time_alarm(const struct alarm_config *alarmConfig, const unsigned int now_sec) {
    unsigned int time_sec = INT_MAX;
    unsigned int diff_sec;
    // Handle timer for Monitoring Time
    if (get_monitoring_time_ts(alarmConfig) != 0) {
        diff_sec = now_sec - get_monitoring_time_start(alarmConfig);

        time_sec = get_monitoring_time_ts(alarmConfig) - diff_sec;
    }
    return time_sec;
}

unsigned int dy9x_alarm(const struct alarm_config *alarmConfig, const unsigned int now_sec) {
    unsigned int time_sec = INT_MAX;
    if ((get_reporting_flags(alarmConfig) & ZJ77_REPORTING_TRIGGERS_DY9X) && (get_meas_DY9Xd(alarmConfig) != 0)) {
        unsigned int diff_sec = now_sec - get_periodic_meas_start(alarmConfig);

        time_sec = get_meas_DY9Xd(alarmConfig) - diff_sec;
    }
    return time_sec;
}

unsigned int time_quota_alarm(const struct alarm_config *alarmConfig, const unsigned int now_sec) {
    unsigned int time_sec = INT_MAX;
    unsigned int op_flags = get_operational_flags(alarmConfig);
    if (duration_measurement_active(alarmConfig)
    && (op_flags & OPERATIONAL_FLAG_TIME_QUOTA_PRESENT) && (get_time_quota(alarmConfig) != 0)) {
        unsigned int pkt_rx_diff = now_sec - get_duration_meas_start(alarmConfig);

        time_sec = get_time_quota(alarmConfig) - (get_duration_meas(alarmConfig) + pkt_rx_diff);
    }
    return time_sec;
}

unsigned int p88n_alarm(const struct alarm_config *alarmConfig, const unsigned int now_sec) {
    unsigned int time_sec = INT_MAX;
    unsigned int local_reporting_flags = get_reporting_flags(alarmConfig);
    if (duration_measurement_active(alarmConfig) && (local_reporting_flags & ZJ77_REPORTING_TRIGGERS_P88N) && (get_time_threshold(alarmConfig) != 0)) {
        unsigned int pkt_rx_diff = now_sec - get_duration_meas_start(alarmConfig);

        time_sec = get_time_threshold(alarmConfig) -
                                ((get_duration_meas(alarmConfig) + get_duration_meas_threshold_used(alarmConfig))
                                 + pkt_rx_diff);
    }
    return time_sec;
}

unsigned int idt_alarm(const struct alarm_config *alarmConfig, const unsigned int now_sec) {
    unsigned int time_sec = INT_MAX;
    if (duration_measurement_active(alarmConfig) && get_idt_alarm_time(alarmConfig) != 0) {
        unsigned int last_pkt_diff = now_sec - get_time_of_last_pkt(alarmConfig);
        time_sec = get_idt_alarm_time(alarmConfig);
        time_sec = time_sec - last_pkt_diff;
    }
    return time_sec;
}


// Renamed parameters to include 'old' and 'new' semantic
void maybe_update_min_value_ms(unsigned long *old_min_value_ms, unsigned int new_value_s) {
    *old_min_value_ms = min(s_to_ms(new_value_s), *old_min_value_ms);
    // unsigned int new_value_ms = s_to_ms(new_value_s);
    // if (new_value_ms < *old_min_value_ms) {
    //     *old_min_value_ms = new_value_ms;
    // }
}

bool get_operational_flag_state(struct alarm_config *pAlarmConfig, unsigned int flag) {
    return pAlarmConfig->operational_flags & flag;
}

unsigned int get_monitoring_time_start(struct alarm_config *pAlarmConfig) {
    return pAlarmConfig->timers->monitoring_time_start;
}

unsigned int get_monitoring_time_ts(struct alarm_config *pAlarmConfig) {
    return pAlarmConfig->timers->monitoring_time_ts;
}

unsigned int get_periodic_meas_start(struct alarm_config *pAlarmConfig) {
    return pAlarmConfig->timers->periodic_meas_start;
}

unsigned int get_meas_DY9Xd(struct alarm_config *pAlarmConfig) {
    return pAlarmConfig->timers->meas_dy9xd;
}

unsigned int get_quota_holding_time(struct alarm_config *pAlarmConfig) {
    return pAlarmConfig->timers->quota_holding_time;
}

unsigned int get_time_quota(struct alarm_config *pAlarmConfig) {
    return pAlarmConfig->time_quota;
}

unsigned int get_duration_meas_threshold_used(struct alarm_config *pAlarmConfig) {
    return pAlarmConfig->timers->duration->meas_threshold_used;
}

unsigned int get_duration_meas(struct alarm_config *pAlarmConfig) {
    return pAlarmConfig->timers->duration->meas;
}

unsigned int get_time_threshold(struct alarm_config *pAlarmConfig) {
    return pAlarmConfig->time_threshold;
}

unsigned int get_idt_alarm_time(struct alarm_config *pAlarmConfig) {
    return pAlarmConfig->idt_alarm_time;
}

unsigned int get_duration_meas_start(struct alarm_config *pAlarmConfig) {
    return pAlarmConfig->timers->duration->meas_start;
}

unsigned int get_reporting_flags(struct alarm_config *pAlarmConfig) {
    return pAlarmConfig->reporting_flags;
}

unsigned int get_time_of_last_pkt(struct alarm_config *pAlarmConfig) {
    return pAlarmConfig->last_pkt;
}

bool duration_measurement_active(struct alarm_config *pAlarmConfig) {
    return pAlarmConfig->timers->duration->meas_active;
}

unsigned int get_operational_flags(struct alarm_config *pAlarmConfig) {
    return pAlarmConfig->operational_flags;
}

unsigned int get_bti_time_interval(struct alarm_config *config) {
    return config->timers->duration->bti_time_interval;
}

void set_duration_meas_active(struct alarm_config *config, bool value) {
    config->timers->duration->meas_active = true;
}

void set_duration_meas_start(struct alarm_config *config, unsigned int value) {
    config->timers->duration->meas_start = value;
}

void set_idt_alarm_time(struct alarm_config *config, unsigned int value) {
    config->idt_alarm_time = value;
}

void set_last_pkt_time(struct alarm_config *config, unsigned int value) {
    config->last_pkt = value;
}

void add_reporting_flag(struct alarm_config *config, unsigned int flag) {
    config->reporting_flags += flag;
}

void set_time_threshold(struct alarm_config *config, unsigned int value) {
    config->time_threshold = value;
}

void set_duration_meas_threshold(struct alarm_config *config, unsigned int value) {
    config->timers->duration->meas_threshold_used = value;
}

void add_operational_flag(struct alarm_config *config, unsigned int flag) {
    config->operational_flags += flag;
}

void set_time_quota(struct alarm_config *config, unsigned int value) {
    config->time_quota = value;
}

void set_duration_meas(struct alarm_config *config, unsigned int value) {
    config->timers->duration->meas = value;
}

void set_quota_holding_time(struct alarm_config *config, unsigned int value) {
    config->timers->quota_holding_time = value;
}

void set_meas_dy9xd(struct alarm_config *config, unsigned int value) {
    config->timers->meas_dy9xd = value;
}

void set_periodig_meas_start(struct alarm_config *config, unsigned int value) {
    config->timers->periodic_meas_start = value;
}

void set_monitoring_time_ts(struct alarm_config *config, unsigned int value) {
    config->timers->monitoring_time_ts = value;
}

void set_monitoring_time_start(struct alarm_config *config, unsigned int value) {
    config->timers->monitoring_time_start = value;
}

void set_bti_time_interval(struct alarm_config *config, unsigned int value) {
    config->timers->duration->bti_time_interval = value;
}

