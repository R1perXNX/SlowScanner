#pragma once

template <class T>
class singleton {
public:

    singleton(const singleton&) = delete;
    singleton& operator=(const singleton&) = delete;


    static T& get_instance() {
        static T instance;
        return instance;
    }

protected:
    singleton() = default;
    ~singleton() = default;
};