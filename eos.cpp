#include <linux/input.h>
#include <chrono>
#include <cmath>
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

const long pwm_led_period	= 2e6;	// 1ms

#define LED_UP		20
#define LED_DOWN	21
#define LED_OFF		22
#define LED_ON		23
#define LED_25		16
#define LED_50		17
#define LED_75		18
#define LED_100		19
#define LED_AUTO	10
#define LED_FLASH	9

// GLOBALS
bool main_closing = 0;

int ir_ev;		// IR event file descriptor

float pwm_led		= 0.5;
bool led_on		= 1;
bool led_flash		= 0;
bool led_stable 	= 0;	// Can we slow down the loop?

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

void init_led()
{
	auto period = to_string(pwm_led_period);
	auto path = pwm_led_chip + "/pwm" + pwm_led_pwmnum;

	echo(pwm_led_chip + "/export", "pwm_led_pwmnum");
	echo(path + "/period", period);
	echo(path + "/duty_cycle", period);
	echo(path + "/polarity", "inversed");
	echo(path + "/enable", "1");
}

void update_led() {
	// Correct the contents of led_pwm if doesn't make sense
	if (pwm_led > 1) pwm_led = 1;
	if (pwm_led < 0.15) pwm_led = 0.15;


	static float pwm = 0;
	static long last_b = 0;
	low_pass(&pwm, led_on ? pwm_led : 0, 15);

	/*
	const float g = 3.75;			// Algorithm from pistackmon, to be adjusted
	float a;
	a = 1 / (exp(g) - 1);
	a *= exp(g*pwm) - 1;
	a *= pwm_led_period;
	a *= 0.5; 		// 50% power cap;
	*/
	float a;
	a = pow(pwm,2.8);			// "gamma"
	a *= pwm_led_period;
	a *= 0.5; 		// 50% power cap;

	long b = floor(a);
	if (led_flash)	echo(pwm_led_chip + "/pwm" + pwm_led_pwmnum + "/duty_cycle", to_string(pwm_led_period));
	else 		echo(pwm_led_chip + "/pwm" + pwm_led_pwmnum + "/duty_cycle", to_string(b));

	led_stable = (last_b == b);
	last_b = b;
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

	if (read(ir_ev, &ev, sizeof ev) == 0) return -1;
	if (ev.value == 0) return -1;
	switch (ev.value) {
	case LED_ON:	led_flash = 0; pwm_led = 0.5; led_on = 1;	
		break;
	case LED_OFF:	led_flash = 0; led_on = 0;
		break;
	case LED_UP:	led_flash = 0; pwm_led += 0.05;
		break;
	case LED_DOWN:	led_flash = 0; pwm_led -= 0.05;
		break;
	case LED_25:	led_flash = 0; pwm_led = 0.25;
		break;
	case LED_50:	led_flash = 0; pwm_led = 0.5;
		break;
	case LED_75:	led_flash = 0; pwm_led = 0.75;
		break;
	case LED_100:	led_flash = 0; pwm_led = 1;
		break;
	case LED_AUTO:	// TODO - implement something if you want
		break;
	case LED_FLASH:	led_flash = 1;
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
		update_led();

		//  -   -   -   -   -   -   -   -   -   -   -   -   -   -   -

		refresh_period = chrono::microseconds(led_stable ? 200000 : 20000);
		next_refresh += refresh_period;
		// Catch up if this thread is running really late
		// Happens if daemon launches before Pi updates real time clock
		if (next_refresh < chrono::high_resolution_clock::now())
			next_refresh = chrono::high_resolution_clock::now() + refresh_period;
		this_thread::sleep_until(next_refresh);
	}
}
