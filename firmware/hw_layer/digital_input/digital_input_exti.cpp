/*
 * digital_input_exti.cpp
 *
 *  Created on: Dec 18, 2018
 * @author Andrey Belomutskiy, (c) 2012-2021
 */

#include "pch.h"

#if HAL_USE_PAL && EFI_PROD_CODE
#include "digital_input_exti.h"

/**
 * EXTI is a funny thing: you can only use same pin on one port. For example, you can use
 * PA0 PB5 PE2 PD7
 * but you cannot use
 * PA0 PB0 PE2 PD7
 * because pin '0' would be used on two different ports
 */

#ifdef EFI_IRQ_EXTI_HANDOFF_PRIORITY
void efiExtiInit() {
	nvicEnableVector(I2C1_EV_IRQn, EFI_IRQ_EXTI_HANDOFF_PRIORITY);
}

struct ExtiChannel {
	ExtiCallback Callback = nullptr;
	void* CallbackData;
	ioline_t Line;

	uint32_t LastEdgeTimestamp;
	uint32_t Count;

	// Name is also used as an enable bit
	const char* Name = nullptr;
};

static ExtiChannel channels[16];

// Capture the level in the fast ISR so it corresponds to the event timestamp.
void efiExtiEnablePin(const char* msg, brain_pin_e brainPin, uint32_t mode, ExtiCallback cb, void* cb_data) {
	/* paranoid check, in case of Gpio::Unassigned getHwPort will return NULL
	 * and we will fail on next check */
	if (!isBrainPinValid(brainPin)) {
		return;
	}

	efiAssertVoid(ObdCode::OBD_PCM_Processor_Fault, msg, "efiExtiEnablePin msg must not be null");

	ioportid_t port = getHwPort(msg, brainPin);
	if (port == NULL) {
		return;
	}

	bool wasUsed = brain_pin_markUsed(brainPin, msg);
	if (wasUsed) {
		// error condition we shall bail
		return;
	}

	int index = getHwPin(msg, brainPin);

	auto& channel = channels[index];

	/* is this index already used? */
	if (channel.Name) {
		firmwareError(
				ObdCode::CUSTOM_ERR_PIN_ALREADY_USED_2,
				"%s: pin %s/index %d: exti index already used by %s",
				msg,
				hwPortname(brainPin),
				index,
				channel.Name);
		return;
	}

	channel.Callback = cb;
	channel.CallbackData = cb_data;
	channel.Name = msg;

	ioline_t line = PAL_LINE(port, index);
	channel.Line = line;

	palSetLineMode(line, PAL_MODE_INPUT);
	palEnableLineEvent(line, mode);
}
void efiExtiDisablePin(brain_pin_e brainPin) {
	/* paranoid check, in case of Gpio::Unassigned getHwPort will return NULL
	 * and we will fail on next check */
	if (!isBrainPinValid(brainPin)) {
		return;
	}

	ioportid_t port = getHwPort("exti", brainPin);
	if (port == NULL) {
		return;
	}
	brain_pin_markUnused(brainPin);

	int index = getHwPin("exti", brainPin);

	auto& channel = channels[index];

	/* is this index was used? */
	if (!channel.Name) {
		return;
	}

	ioline_t line = PAL_LINE(port, index);
	palDisableLineEvent(line);

	/* mark unused */
	channel.Name = nullptr;
	channel.Callback = nullptr;
	channel.CallbackData = nullptr;
}

ExtiEdgeCounter efiExtiGetCounter(brain_pin_e brainPin) {
	if (!isBrainPinValid(brainPin)) {
		return {};
	}

	ioportid_t port = getHwPort("exti", brainPin);
	if (port == NULL) {
		return {};
	}

	int index = getHwPin("exti", brainPin);
	auto& channel = channels[index];

	// Not enabled, or this index belongs to the same pin number on another port
	if (!channel.Name || channel.Line != PAL_LINE(port, index)) {
		return {};
	}

	// The fast ISR runs above the kernel, so chSysLock() doesn't keep it out
	__disable_irq();
	ExtiEdgeCounter result = {channel.Count, channel.LastEdgeTimestamp};
	__enable_irq();

	return result;
}

static inline void triggerInterrupt() {
	// Manually fire the I2C1_EV interrupt, it will be queued after this interrupt returns
	NVIC->STIR = I2C1_EV_IRQn;
}

struct ExtiQueueEntry {
	// Lower 32 bits only, extended to 64 bits in the handoff to keep the fast ISR short
	uint32_t Timestamp;
	uint8_t Channel;
	bool Level;
};

template <typename T, size_t TSize>
class ExtiQueue {
public:
	bool push(const T& val) {
		if ((m_write == m_read - 1) || (m_write == TSize - 1 && m_read == 0)) {
			// queue full, drop
			return false;
		}

		arr[m_write] = val;
		m_write++;

		// wrap end of list
		if (m_write == TSize) {
			m_write = 0;
		}
		return true;
	}

	expected<T> pop() {
		if (m_read == m_write) {
			// Queue empty
			return unexpected;
		}

		T value = arr[m_read];
		m_read++;

		// wrap end of list
		if (m_read == TSize) {
			m_read = 0;
		}

		return value;
	}

private:
	T arr[TSize];

	uint8_t m_read = 0;
	uint8_t m_write = 0;
};

static CCM_OPTIONAL ExtiQueue<ExtiQueueEntry, 32> queue;

static uint8_t overflowCounter = 0;

CH_IRQ_HANDLER(STM32_I2C1_EVENT_HANDLER) {
	OSAL_IRQ_PROLOGUE();

	// Reference for extending the queued 32-bit timestamps. Entries may be stamped before or
	// after this point, so this is correct as long as each is within 2^31 ticks (~9 minutes).
	efitick_t nowNt = getTimeNowNt();

	while (true) {
		// get the timestamp out under lock
		// todo: lock freeeeee!
		__disable_irq();
		auto result = queue.pop();
		__enable_irq();

		// Queue empty, we're done here.
		if (!result) {
			break;
		}

		auto& entry = result.Value;
		efitick_t timestamp = nowNt + (int32_t)(entry.Timestamp - (uint32_t)nowNt);

		auto& channel = channels[entry.Channel];

		if (channel.Callback) {
			channel.Callback(channel.CallbackData, timestamp, entry.Level);
		}
	}

	assertInterruptPriority(__func__, EFI_IRQ_EXTI_HANDOFF_PRIORITY);

	OSAL_IRQ_EPILOGUE();
}

uint8_t getExtiOverflowCounter() {
	return overflowCounter;
}

// The fast path must never touch the FPU: one FP instruction makes the core lazily stack the
// interrupted thread's FP registers. This turns any float use into a compile error, and stops GCC
// from using FP registers for plain copies.
#define EXTI_NO_FPU __attribute__((target("general-regs-only")))

// noinline: inlined into handleExti's loop, GCC hoists the queue and NVIC state into callee-saved
// registers, so every entry pays a 9-register push/pop. Out of line is cheaper for the usual single
// edge per entry.
__attribute__((noinline)) EXTI_NO_FPU static void handleExtiEdge(uint8_t index, uint32_t timestamp) {
	auto& channel = channels[index];

	channel.LastEdgeTimestamp = timestamp;
	channel.Count++;

	if (!channel.Callback) {
		return;
	}

	bool level = palReadLine(channel.Line) == PAL_HIGH;
	if (queue.push({timestamp, index, level})) {
		triggerInterrupt();
	} else if (overflowCounter < 255) {
		// Saturate so a busy input cannot wrap the diagnostic back to zero.
		overflowCounter++;
	}
}

// One handler serves every EXTI vector: it services all 16 lines each time, so it doesn't need to
// know which vector fired. If it services a line belonging to another vector, that vector still
// enters once more afterwards, finds nothing pending, and returns.
// The handler macro expands to an extern "C" definition, which can't carry the attribute itself
extern "C" EXTI_NO_FPU void handleExti(void);

CH_FAST_IRQ_HANDLER(handleExti) {
	// No need to lock anything, we're already the highest-pri interrupt!

	// Stamp before touching EXTI so the slow peripheral accesses don't delay it
	uint32_t timestamp = getTimeNowLowerNt();

	// One read and clear for all lines, then only visit the lines that fired. Edges that arrive
	// after this read leave their bit pending and re-enter the ISR.
	uint32_t pr;
	extiGetAndClearGroup1(0xFFFF, pr);

	while (pr) {
		uint8_t index = __builtin_ctz(pr);
		pr &= pr - 1;

		handleExtiEdge(index, timestamp);
	}
}

// Point each EXTI vector table entry straight at handleExti. These override the weak defaults in
// ChibiOS vectors.S.
#define EXTI_VECTOR(name) extern "C" void name(void) __attribute__((alias("handleExti")))

EXTI_VECTOR(Vector58); // Line 0
EXTI_VECTOR(Vector5C); // Line 1
EXTI_VECTOR(Vector60); // Line 2
EXTI_VECTOR(Vector64); // Line 3
EXTI_VECTOR(Vector68); // Line 4
EXTI_VECTOR(Vector9C); // Lines 5-9
EXTI_VECTOR(VectorE0); // Lines 10-15

#else // not STM32

// TODO: non-stm32 exti
void efiExtiInit() {
	firmwareError("exti not supported");
}

void efiExtiEnablePin(const char*, brain_pin_e, uint32_t, ExtiCallback, void*) {}
void efiExtiDisablePin(brain_pin_e) {}

ExtiEdgeCounter efiExtiGetCounter(brain_pin_e) {
	return {};
}

uint8_t getExtiOverflowCounter() {
	return 0;
}

#endif

#endif /* HAL_USE_PAL && EFI_PROD_CODE */
