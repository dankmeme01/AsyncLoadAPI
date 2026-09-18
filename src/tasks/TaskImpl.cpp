#include "TaskImpl.hpp"

using namespace geode::prelude;

namespace AsyncLoad {

TaskStatus TaskHandle::status() const {
    return m_ctl ? m_ctl->m_status.load(std::memory_order::acquire) : TaskStatus::Cancelled;
}

uint64_t TaskHandle::id() const {
    return m_ctl ? m_ctl->m_id : 0;
}

std::string TaskHandle::error() const {
    return m_ctl ? m_ctl->error() : std::string{};
}

void TaskHandle::cancel() const {
    if (m_ctl) m_ctl->cancel();
}

void TaskHandle::setName(std::string name) const {
    if (m_ctl) m_ctl->m_name = std::move(name);
}

std::string TaskHandle::name() const {
    if (!m_ctl) return "";
    return m_ctl->name();
}

}
