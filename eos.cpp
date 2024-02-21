#include <linux/input.h>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <bits/stdc++.h>
#include <stdio.h>
#include <string>
#include <thread>
#include <unistd.h>

using namespace std;

const string ir_protocols	= "/sys/class/rc/rc0/protocols";
const char * ir_event		= "/dev/input/by-path/platform-fdd70030.pwm-event";
const string pwm_led_chip	= "/sys/class/pwm/pwmchip0";
const string pwm_led_pwmnum	= "0";

// Variable PWM period
// Prefers max LED period
// If duty_cycle falls below pwm_led_min, period is increased
const long pwm_led_period_min	= 20e6;	// in nanoseconds
const long pwm_led_period_max	= 2e6;	// in nanoseconds
const long pwm_led_min		= 5000;	// minimum dc setting
const double pwm_led_dc_min	= double(pwm_led_min) / double(pwm_led_period_min);

// Auto Mode constants
const float dawn		= 4;
const float sunrise		= 6;
const float autooff		= 7;
const float sunset		= 18;
const float dusk		= 20;

const float sunrise_pwm		= 0.6;

#define LED_UP		5
#define LED_DOWN	4
#define LED_OFF		6
#define LED_ON		7
#define LED_25		9
#define LED_50		8
#define LED_75		10
#define LED_100		11
#define LED_AUTO	19	// Auto Mode
#define LED_FLASH	15


// GLOBALS
bool main_closing = 0;

int ir_ev;		// IR event file descriptor

float pwm_led		= 0.2;
bool led_on		= 1;
bool led_flash		= 0;
bool led_stable 	= 0;	// Can we slow down the loop?
bool auto_mode	= 0;
float sunset_pwm = 0.3;

// hr() variables
std::time_t t = std::time(0);   // get time now
std::tm* now = std::localtime(&t);


void echo(const string path, const string input)
	// Echoes data into a file
{
	auto fd = fopen(path.data(), "w");
	if (!fd) {
		cerr << "Error: Cannot open \"" << path << "\"\n";
		exit(-1);
	}

	if (fputs(input.data(), fd) < 0) {
		cerr << "Error: Cannot write \"" << input << "\" to \"" << path << "\"\n";
		exit(-2);
	}
	fclose(fd);
}

void low_pass(float * var, float input, float a)
	// "a" is a product of time constant and sampling frequency
{
	a = 1 - exp(-1/a);
	*var = (a * input) + ((1-a) * *var);
}

// Get current time in hours (float)
float hr() {
    t = std::time(0);   // get time now
    float a = now->tm_hour;
    a += float(now->tm_min)/60;
    a += float(now->tm_sec)/3600;
    return a;
}

void init_led()
{
	auto period = to_string(pwm_led_period_min);
	auto path = pwm_led_chip + "/pwm" + pwm_led_pwmnum;

	echo(pwm_led_chip + "/export", pwm_led_pwmnum);
	echo(path + "/period", period);
	echo(path + "/duty_cycle", "0");
	echo(path + "/polarity", "inversed");
	echo(path + "/enable", "1");
}

void update_led() {
	// Correct the contents of led_pwm if doesn't make sense
	if (pwm_led > 1) pwm_led = 1;
	if (pwm_led < 0) pwm_led = 0;


	static float pwm = 0;
	static float last_dc = 0;
	low_pass(&pwm, led_on ? pwm_led : 0, 15);

	float a;
	a = pow(pwm,2.8);			// "gamma"
	a *= 0.5; 				// 50% power cap;
	if (led_on) a += pwm_led_dc_min;	// Ensure lights are at least minimally on when they're on
	
	long dc, prd;
	prd = pwm_led_period_max;
	dc = a * pwm_led_period_max;
	if (dc < pwm_led_min) {
		dc = pwm_led_min;
		if (a > 0) prd = dc / a;
		else prd = pwm_led_period_min + 1;
	}
	if (prd > pwm_led_period_min)	dc = 0;	// Should happen when LEDs dim down to 0%


	std::cout << "prd: " << prd << ", dc:" << dc << "\n";
			echo(pwm_led_chip + "/pwm" + pwm_led_pwmnum + "/period", to_string(prd));
	if (led_flash)	echo(pwm_led_chip + "/pwm" + pwm_led_pwmnum + "/duty_cycle", to_string(prd));
	else 		echo(pwm_led_chip + "/pwm" + pwm_led_pwmnum + "/duty_cycle", to_string(dc));

	led_stable = (last_dc == float(dc) / float(prd));
	last_dc = float(dc) / float(prd);
}

void do_auto_mode() {
	float now = hr();
	if (now < dawn) {
		led_on = 0;
		pwm_led = 0;
		return;
	}
	if (now > dawn && now < sunrise) {
		led_on = 1;
		pwm_led = sunrise_pwm * (now - dawn) / (sunrise - dawn);
		return;
	}
	if (now > sunrise  && now < autooff) {
		led_on = 1;
		pwm_led = sunrise_pwm;
		return;
	}
	if (now > autooff && now < autooff+1) {
		led_on = 0;
		return;
	}
	if (now > sunset && now < dusk) {
		led_on = 1;
		pwm_led = sunset_pwm * (1 - ((now - sunset) / (dusk - sunset)));
		return;
	}
	if (now > dusk) {
		led_on = 0;
		return;
	}
}

void init_ir()
{
	echo(ir_protocols, "nec");
	if ((ir_ev = open(ir_event, O_RDONLY | O_NONBLOCK)) == -1) {
		cerr << "Error: Cannot open IR receiver event file.\n";
		exit(-1);
	}
}

int fetch_ir()
{
	struct input_event ev;
	int rd;

	if (read(ir_ev, &ev, sizeof ev) <= 0) return -1;
	//std::cout << "IR Event: " << ", " << ev.type << ", " << ev.code << ", " << ev.value << "\n";
	if (ev.value == 0) return -1;
	switch (ev.value) {
	case LED_ON:	led_flash = 0; pwm_led = 0.3;	led_on = 1;	auto_mode = 0;
		break;
	case LED_OFF:	led_flash = 0; pwm_led = 0.3;	led_on = 0;	auto_mode = 0;
		break;
	case LED_UP:	led_flash = 0; pwm_led += 0.02;	led_on = 1;	auto_mode = 0;
		break;
	case LED_DOWN:	led_flash = 0; pwm_led -= 0.02;	led_on = 1;	auto_mode = 0;
		break;
	case LED_25:	led_flash = 0; pwm_led = 0.25;	led_on = 1;	auto_mode = 0;
		break;
	case LED_50:	led_flash = 0; pwm_led = 0.50;	led_on = 1;	auto_mode = 0;
		break;
	case LED_75:	led_flash = 0; pwm_led = 0.75;	led_on = 1;	auto_mode = 0;
		break;
	case LED_100:	led_flash = 0; pwm_led = 1;	led_on = 1;	auto_mode = 0;
		break;
	case LED_AUTO:	// Sleep Timer
		led_flash = 0;
		led_on = 1;
		sunset_pwm = pwm_led; if (sunset_pwm < 0.2) sunset_pwm = 0.2;
		auto_mode = 1;	
		break;
	case LED_FLASH:	led_flash = 1;			led_on = 1;	auto_mode = 0;
		break;
			// Any other button turns on dim light
	default:	led_flash = 0; pwm_led = 0.01;	led_on = 1;	auto_mode = 0;
		break;
	}
	return ev.value;
}

void signal_handle(const int s) {
	// Handles a few POSIX signals, asking the process to die gracefully
	
	main_closing = 1;

	if (s){};	// Suppress warning about unused, mandatory parameter
}

//  -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   

int main()
{
	signal (SIGINT, signal_handle);		// Catches SIGINT (ctrl+c)
	signal (SIGTERM, signal_handle);	// Catches SIGTERM

	init_ir();
	init_led();

	auto next_refresh = chrono::high_resolution_clock::now();
	auto refresh_period = chrono::microseconds(20000);

	while (!main_closing) {
		fetch_ir();
		if (auto_mode) do_auto_mode();
		update_led();

		//  -   -   -   -   -   -   -   -   -   -   -   -   -   -   -

		refresh_period = chrono::microseconds(led_stable ? 50000 : 20000);
		next_refresh += refresh_period;
		// Catch up if this thread is running really late
		// Happens if daemon launches before SBC updates real time clock
		if (next_refresh < chrono::high_resolution_clock::now())
			next_refresh = chrono::high_resolution_clock::now() + refresh_period;
		this_thread::sleep_until(next_refresh);
	}
}
