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

const long pwm_led_period	= 10e6;	// in nanoseconds
const long pwm_led_min		= 4500;	// minimum period setting

// Sleep timer time constants (in seconds)
const auto st_t1		= chrono::milliseconds(700);	// Acknowledge blink
const auto st_t2		= chrono::seconds(10);		// Time before lights go out
const auto st_t3		= chrono::seconds(int(7 * 3600));	// dawn - minimum brightness
const auto st_t4		= chrono::seconds(int(8 * 3600));	// brightness ramp begins
const auto st_t5		= chrono::seconds(int(10 * 3600));	// st_pwmon is reached
const auto st_t6		= chrono::seconds(11 * 3600);	// Automatically turns off lights and exits sleep timer mode
// Sleep timer - other constants
const float st_pwmon		= 0.7;		// brigtness after t5


#define LED_UP		5
#define LED_DOWN	4
#define LED_OFF		6
#define LED_ON		7
#define LED_25		9
#define LED_50		8
#define LED_75		10
#define LED_100		11
#define LED_AUTO	19	// Sleep timer
#define LED_FLASH	15


// GLOBALS
bool main_closing = 0;

int ir_ev;		// IR event file descriptor

float pwm_led		= 0.2;
bool led_on		= 1;
bool led_flash		= 0;
bool led_stable 	= 0;	// Can we slow down the loop?


bool sleep_timer	= 0;
auto st_begin  = chrono::system_clock::now();


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

	echo(pwm_led_chip + "/export", pwm_led_pwmnum);
	echo(path + "/period", period);
	echo(path + "/duty_cycle", period);
	echo(path + "/polarity", "inversed");
	echo(path + "/enable", "1");
}

void update_led() {
	// Correct the contents of led_pwm if doesn't make sense
	if (pwm_led > 1) pwm_led = 1;
	if (pwm_led < 0.05) pwm_led = 0.05;


	static float pwm = 0;
	static long last_b = 0;
	low_pass(&pwm, led_on ? pwm_led : 0, 15);

	float a;
	a = pow(pwm,2.8);			// "gamma"
	a *= 0.5; 				// 50% power cap;
	a *= (pwm_led_period - pwm_led_min);
	if (led_on) a += pwm_led_min;

	long b = floor(a);
	if (led_flash)	echo(pwm_led_chip + "/pwm" + pwm_led_pwmnum + "/duty_cycle", to_string(pwm_led_period));
	else 		echo(pwm_led_chip + "/pwm" + pwm_led_pwmnum + "/duty_cycle", to_string(b));

	led_stable = (last_b == b);
	last_b = b;
}

void do_sleep_timer() {
	auto now = chrono::high_resolution_clock::now();
	if (now - st_begin < st_t1) {
		led_on = 1;
		pwm_led = 0.4;
		return;
	}
	if (now - st_begin < st_t2) {
		led_on = 1;
		pwm_led = 0.05;
		return;
	}
	if (now - st_begin < st_t3) {
		led_on = 0;
		return;
	}
	if (now - st_begin < st_t4) {
		led_on = 1;
		pwm_led = 0.05;
		return;
	}
	if (now - st_begin < st_t5) {
		float a = chrono::duration_cast<chrono::milliseconds>((now - st_begin) - st_t3).count();
		float b = chrono::duration_cast<chrono::milliseconds>(st_t4 - st_t3).count();
		pwm_led = (a/b) * st_pwmon;
		led_on = 1;
		return;
	}
	if (now - st_begin >= st_t6) {
		pwm_led = 0.2;
		led_on = 0;
		sleep_timer = 0;
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
	case LED_ON:	led_flash = 0; pwm_led = 0.3;	led_on = 1;	sleep_timer = 0;
		break;
	case LED_OFF:	led_flash = 0; pwm_led = 0.3;	led_on = 0;	sleep_timer = 0;
		break;
	case LED_UP:	led_flash = 0; pwm_led += 0.05;	led_on = 1;	sleep_timer = 0;
		break;
	case LED_DOWN:	led_flash = 0; pwm_led -= 0.05;	led_on = 1;	sleep_timer = 0;
		break;
	case LED_25:	led_flash = 0; pwm_led = 0.05;	led_on = 1;	sleep_timer = 0;
		break;
	case LED_50:	led_flash = 0; pwm_led = 0.33;	led_on = 1;	sleep_timer = 0;
		break;
	case LED_75:	led_flash = 0; pwm_led = 0.66;	led_on = 1;	sleep_timer = 0;
		break;
	case LED_100:	led_flash = 0; pwm_led = 1;	led_on = 1;	sleep_timer = 0;
		break;
	case LED_AUTO:	// Sleep Timer
		sleep_timer = 1;	
		st_begin  = chrono::system_clock::now();
		break;
	case LED_FLASH:	led_flash = 1;			led_on = 1;	sleep_timer = 0;
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
		if (sleep_timer) do_sleep_timer();
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
