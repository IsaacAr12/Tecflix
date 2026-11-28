#include <gtest/gtest.h>

#include <atomic>
#include <chrono>

#include "pool_transcodificacion.hpp"

using namespace tecflix::streaming;

TEST(PoolTranscodificacion, EjecutaTodosLosTrabajos) {
    std::atomic<int> contador{0};
    PoolTranscodificacion pool(3);
    for (int i = 0; i < 50; ++i) pool.encolar([&] { ++contador; });
    pool.esperarVacia();
    EXPECT_EQ(contador.load(), 50);
    EXPECT_EQ(pool.pendientes(), 0u);
}

TEST(PoolTranscodificacion, UnErrorNoDetieneLaCola) {
    std::atomic<int> contador{0};
    PoolTranscodificacion pool(0);  // 0 se convierte en 1 hilo
    pool.encolar([] { throw std::runtime_error("falla"); });
    pool.encolar([] { throw 42; });
    pool.encolar([&] { ++contador; });
    pool.esperarVacia();
    EXPECT_EQ(contador.load(), 1);
}

TEST(PoolTranscodificacion, CuentaPendientesYDescartaAlDestruir) {
    std::atomic<bool> empezo{false};
    std::atomic<bool> liberar{false};
    std::atomic<int> ejecutados{0};
    {
        PoolTranscodificacion pool(1);
        pool.encolar([&] {
            empezo = true;
            while (!liberar) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ++ejecutados;
        });
        pool.encolar([&] { ++ejecutados; });
        while (!empezo) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        EXPECT_EQ(pool.pendientes(), 2u);  // uno corriendo + uno en espera
        liberar = true;
        // Al salir, el destructor espera el trabajo en curso; el pendiente puede descartarse.
    }
    EXPECT_GE(ejecutados.load(), 1);
}
