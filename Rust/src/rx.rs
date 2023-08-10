#![no_std]
#![no_main]
#![feature(type_alias_impl_trait)]

use embassy_executor::_export::StaticCell;
use esp32c3_hal as hal;

use embassy_executor::Executor;
use embassy_time::Instant;
use esp_backtrace as _;
use esp_println::logger::init_logger;
use esp_wifi::esp_now::{EspNow, BROADCAST_ADDRESS, WifiPhyRate, self, PeerInfo};
use esp_wifi::EspWifiInitFor;
use hal::clock::{ClockControl, CpuClock};
use hal::Rng;
use hal::efuse::Efuse;
use hal::systimer::SystemTimer;
use hal::{embassy, peripherals::Peripherals, prelude::*, timer::TimerGroup, Rtc};


#[embassy_executor::task]
async fn run(mut esp_now: EspNow<'static>) {
    let mut cnt: u64 = 0;

    loop {
        let _data = esp_now.receive_async().await;
        cnt += 1;

        if cnt % 250 == 0 {
            log::info!("recv cnt: {}", cnt);
        }
    }
}

static EXECUTOR: StaticCell<Executor> = StaticCell::new();

#[entry]
fn main() -> ! {
    init_logger(log::LevelFilter::Info);

    let peripherals = Peripherals::take();

    let mut system = peripherals.SYSTEM.split();
    let clocks = ClockControl::configure(system.clock_control, CpuClock::Clock160MHz).freeze();

    let mut rtc = Rtc::new(peripherals.RTC_CNTL);
    let timer_group0 = TimerGroup::new(
        peripherals.TIMG0,
        &clocks,
        &mut system.peripheral_clock_control,
    );
    let mut wdt0 = timer_group0.wdt;
    let timer_group1 = TimerGroup::new(
        peripherals.TIMG1,
        &clocks,
        &mut system.peripheral_clock_control,
    );
    let mut wdt1 = timer_group1.wdt;
    
    // disable watchdog timers
    rtc.swd.disable();
    rtc.rwdt.disable();
    wdt0.disable();
    wdt1.disable();
    log::info!("hardware watchdogs disabled.");

    embassy::init(&clocks, timer_group0.timer0);
    log::info!("embassy initialized.");

    let timer = SystemTimer::new(peripherals.SYSTIMER).alarm0;
    let init = esp_wifi::initialize(
        EspWifiInitFor::Wifi,
        timer,
        Rng::new(peripherals.RNG),
        system.radio_clock_control,
        &clocks,
    ).unwrap();
    log::info!("wifi initialized.");

    let (wifi, _) = peripherals.RADIO.split();
    let esp_now = esp_now::EspNow::new(&init, wifi).unwrap();
    log::info!("esp-now version {}", esp_now.get_version().unwrap());
    // esp_now.set_rate(WifiPhyRate::Rate48m).unwrap();
    // esp_now.set_rate(WifiPhyRate::RateMcs7Sgi).unwrap();
    log::info!("esp_now initialized: {:?}.", Efuse::get_mac_address());
    
    let executor = EXECUTOR.init(Executor::new());
    executor.run(|spawner| {
        spawner.spawn(run(esp_now)).ok();
    })
}
