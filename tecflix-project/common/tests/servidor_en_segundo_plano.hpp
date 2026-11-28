#ifndef TECFLIX_TESTS_SERVIDOR_EN_SEGUNDO_PLANO_HPP
#define TECFLIX_TESTS_SERVIDOR_EN_SEGUNDO_PLANO_HPP

#include <memory>
#include <string>
#include <thread>

#include "tecflix/servidor_http.hpp"

namespace tecflix::pruebas {

// Levanta un ServidorHttp en un puerto libre mientras dure el objeto (RAII) y
// entrega un cliente HTTP apuntando a él.
class ServidorEnSegundoPlano {
public:
    explicit ServidorEnSegundoPlano(ServidorHttp& servidor) : servidor_(servidor) {
        puerto_ = servidor_.tomarPuertoLibre("127.0.0.1");
        hilo_ = std::thread([this] { servidor_.escucharEnPuertoTomado(); });
        servidor_.esperarListo();
    }

    ~ServidorEnSegundoPlano() {
        servidor_.detener();
        hilo_.join();
    }

    ServidorEnSegundoPlano(const ServidorEnSegundoPlano&) = delete;
    ServidorEnSegundoPlano& operator=(const ServidorEnSegundoPlano&) = delete;

    int puerto() const { return puerto_; }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(puerto_); }
    std::unique_ptr<httplib::Client> cliente() const {
        return std::make_unique<httplib::Client>("127.0.0.1", puerto_);
    }

private:
    ServidorHttp& servidor_;
    int puerto_ = 0;
    std::thread hilo_;
};

}  // namespace tecflix::pruebas

#endif  // TECFLIX_TESTS_SERVIDOR_EN_SEGUNDO_PLANO_HPP
