#pragma once

#include "SystemCScheduler.hh"
#include "TimeConversion.hh"
#include "basics.h"

#include "astra-sim/system/Common.hh"
#include <cstdint>

const size_t BYTES_PER_COUNT = 1;

struct PacketizationCount {
    std::size_t packets = 0;
    std::size_t cells = 0;
};

struct PendingMessage {
    size_t messageId;
    double time = 0.0;
    size_t src = 0;
    size_t dst = 0;
    int tag;
    // size_t messageSizeBytes = 0;
    PacketizationCount messageSize;
    int type;

    sim_request* request;

    void (*callback)(void* callbackArg);
    void* callbackArg;
    size_t returnToRank = 0;
};

struct MessageReceiveState {
    PacketizationCount expectedSize;

    std::size_t receivedCells = 0;
    std::size_t receivedPackets = 0;

    bool complete = false;
};

struct ActiveMessage {
    bool valid = false;
    std::size_t startTime = 0;
    std::size_t src = 0;
    std::size_t dst = 0;
    std::string messageId;
    std::size_t remainingCells = 0;
    std::size_t remainingPackets = 0;
    std::size_t currentCellInPacket = 0;
    std::size_t currentPacketID = 0;
};

class PacketGeneratorInterface {
  public:
    virtual ~PacketGeneratorInterface() = default;

    /**
     * This function needs to receive information about a message being sent,
     * and inject it into the packet generator. functionality: 1-set a time
     * stamp for the message, 2- create a message ID, 3- finalize messageRecord
     * and inject the message into the packet generator
     */
    void inject_message(size_t messageId,
                        int src,
                        int dst,
                        int tag,
                        uint64_t count,
                        int type,
                        sim_request* request,
                        void (*msg_handler)(void* fun_arg),
                        void* fun_arg,
                        int returnToRank);

    size_t generate_message_id();
    timespec_t get_current_time();

    std::vector<Packet> generatePacketVector(Packets& packets);

  private:
    size_t nextMessageId = 0;
    std::vector<ActiveMessage> m_activeMessages;
    std::vector<std::deque<PendingMessage>> m_pendingMessages;
    std::unordered_map<std::size_t, MessageReceiveState> m_receivedMessages;

    // std::size_t calculateCellsInCurrentPacket(std::size_t remainingBytes)
    // const;
    // PacketizationCount calculatePacketizationCount(std::size_t
    // messageSizeBytes) const;

    std::size_t m_numInputPorts = 0;
    std::size_t m_cellBytes = 8;
    double m_clockPeriod = 0.0;
    double m_currentTime = 0.0;
    std::size_t m_cellsPerPacket = 10;
    bool m_hasPayload = true;

    std::size_t m_currentCycle = 0;
    std::size_t m_nextPacketID = 0;
    std::size_t m_nextMessageIndex = 0;
};

std::vector<Packet> PacketGeneratorInterface::generatePacketVector(
    Packets& packets) {

    for (std::size_t src = 0; src < m_numInputPorts; ++src) {
        if (!m_activeMessages[src].valid && !m_pendingMessages[src].empty()) {
            startNextMessage(src);
        }

        if (!m_activeMessages[src].valid) {
            continue;
        }

        ActiveMessage& active = m_activeMessages[src];
        packets[src].setCreationTimestamp(active.startTime);
        packets[src].setSourcePort(src);
        packets[src].setDestinationPort(active.dst);
        // packets[src].setMessagePacketIdx(active.messageId, active.index); // TODO: packet MOD

        if (m_hasPayload) {  // TODO: m_hasPayload should always be true in this
                             // class`
            if (active.currentCellInPacket == 0) {
                packets[src].setPacketType(PacketType::HEAD);
                packets[src].setID(++m_nextPacketID);
                m_activeMessages[src].currentPacketID = packets[src].getID();
            } else if (active.currentCellInPacket == m_cellsPerPacket - 1) {
                packets[src].setPacketType(PacketType::TAIL);
                packets[src].setID(m_activeMessages[src].currentPacketID);
            } else {
                packets[src].setPacketType(PacketType::PAYLOAD);
                packets[src].setID(m_activeMessages[src].currentPacketID);
            }

            active.currentCellInPacket++;
        }

        packets[src].setFixedHeader(true);

        active.remainingCells -= 1;

        if (m_hasPayload) {
            if (active.currentCellInPacket == m_cellsPerPacket) {
                active.currentCellInPacket = 0;
                active.remainingPackets -= 1;
                ASSERT_PRINT(packets[src].getPacketType() == PacketType::TAIL,
                             "Message ended but last packet is not a TAIL");
            }
        }

        if (active.remainingCells <= 0) {
            ASSERT_PRINT(packets[src].getPacketType() == PacketType::TAIL,
                         "Message ended but last packet is not a TAIL");
            ASSERT_PRINT(active.remainingPackets == 0,
                         "Message ended but has remaining packets");

            m_activeMessages[src] = ActiveMessage{};
        }
    }

    m_currentTime += m_clockPeriod;
    m_currentCycle++;
    return packets;
}

void receivePacketVector(const std::vector<Packet>& packets) {
    for (const Packet& packet : packets) {
        if (!packet.isValid()) {
            continue;
        }

        const size_t messageId = packet.getMessageId();  // TODO: packet MOD

        auto it = m_receivedMessages.find(messageId);

        if (it == m_receivedMessages.end()) {
            continue;
        }

        MessageReceiveState& state = it->second;

        if (state.complete) {
            continue;
        }

        state.receivedCells++;

        if (m_hasPayload) {
            if (packet.getPacketType() == PacketType::TAIL) {
                state.receivedPackets++;
            }
        } else {
            state.receivedPackets++;
        }

        if (state.receivedCells >= state.expectedSize.cells &&
            state.receivedPackets >= state.expectedSize.packets) {
            state.complete = true;
            notify_message_complete(messageId);
            m_receivedMessages.erase(it);
        }
    }
}

size_t PacketGeneratorInterface::generate_message_id() {
    return nextMessageId++;
}

timespec_t PacketGeneratorInterface::get_current_time() {
    return systemCTimeToTimespec(sc_core::sc_time_stamp());
}

void PacketGeneratorInterface::inject_message(
    size_t messageId,
    int src,
    int dst,
    int tag,
    uint64_t count,
    int type,
    sim_request* request,
    void (*msg_handler)(void* fun_arg),
    void* fun_arg,
    int returnToRank) {

    ASSERT_PRINT(src < m_numInputPorts, "src out of range");
    ASSERT_PRINT(dst < m_numInputPorts, "dst out of range");
    ASSERT_PRINT(count > 0, "count must be > 0");
    ASSERT_PRINT(!messageId.empty(), "messageId must not be empty");

    PendingMessage newMessage;
    size_t numPackets =
        (count * BYTES_PER_COUNT) / (m_cellsPerPacket * m_cellBytes);
    PacketizationCount newMessagePacketization = {
        numPackets, numPackets * m_cellsPerPacket};
    newMessage.time = get_current_time().time_val;
    newMessage.messageId = messageId;
    newMessage.src = std::size_t(src);
    newMessage.dst = std::size_t(dst);
    newMessage.tag = tag;
    newMessage.messageSize = newMessagePacketization;
    newMessage.type = type;
    newMessage.request = request;
    newMessage.callback = msg_handler;
    newMessage.callbackArg = fun_arg;
    newMessage.returnToRank = std::size_t(returnToRank);

    m_pendingMessages[src].push_back(newMessage);

    MessageReceiveState rxState;
    rxState.expectedSize = newMessagePacketization;

    m_receivedMessages[messageId] = rxState;
}

void PacketGeneratorInterface::startNextMessage(std::size_t src) {
    const PendingMessage message = m_pendingMessages[src].front();
    m_pendingMessages[src].pop_front();

    m_activeMessages[src] = ActiveMessage{true,
                                          m_currentCycle,
                                          message.src,
                                          message.dst,
                                          message.messageId,
                                          message.messageSize.cells,
                                          message.messageSize.packets,
                                          0,
                                          0};
}

/*
std::size_t PacketGeneratorInterface::calculateCellsInCurrentPacket(
    std::size_t remainingBytes) const {
    if (!m_hasPayload) {
        return 1;
    }
    const std::size_t fullPacketBytes = m_cellsPerPacket * m_cellBytes;

    std::size_t bytesInCurrentPacket =
        std::min(remainingBytes, fullPacketBytes);

    // Force every packet to have at least 2 cells if it is non-empty.
    // This creates a HEAD and a TAIL even for the last short packet.
    if (bytesInCurrentPacket > 0 && bytesInCurrentPacket < 2 * m_cellBytes) {
        bytesInCurrentPacket = 2 * m_cellBytes;
    }

    const std::size_t cellsInPacket =
        (bytesInCurrentPacket + m_cellBytes - 1) / m_cellBytes;

    return cellsInPacket;
}

PacketizationCount PacketGeneratorInterface::calculatePacketizationCount(
    std::size_t messageSizeBytes) const {
    PacketizationCount count;

    if (!m_hasPayload) {
        count.packets = messageSizeBytes;
        count.cells = messageSizeBytes;
        return count;
    }

    ASSERT_PRINT(messageSizeBytes > 0, "messageSizeBytes must be > 0");

    const std::size_t fullPacketBytes = m_cellsPerPacket * m_cellBytes;

    const std::size_t fullPackets = messageSizeBytes / fullPacketBytes;
    const std::size_t remainderBytes = messageSizeBytes % fullPacketBytes;

    count.packets = fullPackets;
    count.cells = fullPackets * m_cellsPerPacket;

    if (remainderBytes > 0) {
        const std::size_t finalPacketCells =
            calculateCellsInCurrentPacket(remainderBytes);

        count.packets += 1;
        count.cells += finalPacketCells;
    }

    return count;
}*/

void PacketGeneratorInterface::notify_message_complete(size_t messageId) {
    auto it = m_pendingMessages.find(messageId);

    if (it == m_pendingMessages.end()) {
        std::printf("Message ID %zu not found in pending messages of rank %d\n",
                    messageId, rank);
        ASSERT_PRINT(false, "Message ID not found in pending.");
        return;
    }

    auto callback = it->second.callback;
    auto arg = it->second.callbackArg;

    callback(arg);
    m_pendingMessages.erase(it);
}
