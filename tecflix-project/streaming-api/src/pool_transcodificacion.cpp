#include "pool_transcodificacion.hpp"

#include <iostream>

namespace tecflix::streaming {

PoolTranscodificacion::PoolTranscodificacion(size_t hilos) {
    if (hilos == 0) hilos = 1;
    for (size_t i = 0; i < hilos; ++i) hilos_.emplace_back([this] { ciclo(); });
}

PoolTranscodificacion::~PoolTranscodificacion() {
    {
        std::lock_guard<std::mutex> bloqueo(mutex_);
        deteniendo_ = true;
    }
    hayTrabajo_.notify_all();
    for (auto& hilo : hilos_) hilo.join();
}

void PoolTranscodificacion::encolar(Trabajo trabajo) {
    {
        std::lock_guard<std::mutex> bloqueo(mutex_);
        cola_.push(std::move(trabajo));
    }
    hayTrabajo_.notify_one();
}

size_t PoolTranscodificacion::pendientes() const {
    std::lock_guard<std::mutex> bloqueo(mutex_);
    return cola_.size() + enEjecucion_;
}

void PoolTranscodificacion::esperarVacia() {
    std::unique_lock<std::mutex> bloqueo(mutex_);
    vacia_.wait(bloqueo, [this] { return cola_.empty() && enEjecucion_ == 0; });
}

void PoolTranscodificacion::ciclo() {
    while (true) {
        Trabajo trabajo;
        {
            std::unique_lock<std::mutex> bloqueo(mutex_);
            hayTrabajo_.wait(bloqueo, [this] { return deteniendo_ || !cola_.empty(); });
            if (deteniendo_) return;
            trabajo = std::move(cola_.front());
            cola_.pop();
            ++enEjecucion_;
        }
        try {
            trabajo();
        } catch (const std::exception& e) {
            std::cerr << "[pool] trabajo terminó con error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[pool] trabajo terminó con un error desconocido" << std::endl;
        }
        {
            std::lock_guard<std::mutex> bloqueo(mutex_);
            --enEjecucion_;
        }
        vacia_.notify_all();
    }
}

}  // namespace tecflix::streaming
