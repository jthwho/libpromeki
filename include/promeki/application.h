/**
 * @file      application.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class UUID;

/**
 * @brief Promeki application lifecycle manager.
 *
 * You must create this object before using any promeki library calls. It must
 * live for the entire lifetime you're using the promeki library. Typically,
 * this object would be created on the stack in the main() function so it
 * leaves scope right before application termination.
 */
class Application {
    public:

        /**
         * @brief Returns the current application object pointer.
         * @return Pointer to the active Application, or nullptr if none exists.
         */
        static Application *current();

        /** @brief Constructs and activates the Application instance. */
        Application();

        /** @brief Destroys the Application instance and releases resources. */
        ~Application();

        /**
         * @brief Returns the application UUID used as a namespace for UUID v3/v5 generation.
         * @return The application namespace UUID.
         */
        const UUID &appUUID() const;

        /**
         * @brief Sets the application UUID used as a namespace for UUID v3/v5 generation.
         * @param uuid The namespace UUID to use.
         */
        void setAppUUID(const UUID &uuid);

        /**
         * @brief Returns the application name used for UUID v3/v5 generation.
         * @return The application name string.
         */
        const String &appName() const;

        /**
         * @brief Sets the application name used for UUID v3/v5 generation.
         * @param name The application name to use.
         */
        void setAppName(const String &name);

    private:
        struct Data;
        Data *_d;
};

PROMEKI_NAMESPACE_END
