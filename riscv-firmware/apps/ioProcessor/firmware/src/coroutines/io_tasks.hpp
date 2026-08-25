#ifndef IOPROCESSOR_IO_TASKS_HPP
#define IOPROCESSOR_IO_TASKS_HPP

#include <abstractx/coro.hpp>
#include "ringbuffer.hpp"

namespace fc::coroutines {

/* PCIe TLP Bridge Task (Dual-SPI0 with FPGA on CS0 Pin 24) */
abstractx::Task<void> fpga_pcie_tlp_task(ipc::SpscRingBuffer *rx_ring, ipc::SpscRingBuffer *tx_ring);

/* Raw IMU SPI Acquisition Task (Single-SPI0 on CS1 Pin 26) */
abstractx::Task<void> imu_sensor_task(ipc::SpscRingBuffer *tx_ring);

/* Raw UART2 Serial Stream Ingestion Task (Pins 11 & 13 with RTO Timeout) */
abstractx::Task<void> uart_stream_task(ipc::SpscRingBuffer *tx_ring);

} // namespace fc::coroutines

#endif // IOPROCESSOR_IO_TASKS_HPP
