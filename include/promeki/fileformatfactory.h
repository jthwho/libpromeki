/**
 * @file      fileformatfactory.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/logger.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

class IODevice;

/**
 * @brief Generic factory template for file-format-specific product creation.
 * @ingroup io
 *
 * FileFormatFactory captures the registry/lookup pattern used by audio and
 * image file backends. Subclasses register themselves at static init time
 * via PROMEKI_REGISTER_FILE_FORMAT_FACTORY and override canDoOperation()
 * and createForOperation() to handle specific file formats.
 *
 * @tparam Product The type that factories create (e.g. AudioFile).
 *
 * @code
 * // Define a factory registration macro for your product type:
 * #define PROMEKI_REGISTER_MYFILE_FACTORY(name) \
 *     PROMEKI_REGISTER_FILE_FORMAT_FACTORY(MyFile, name)
 *
 * // Subclass and register:
 * class MyFileFactory_Foo : public FileFormatFactory<MyFile> {
 *     MyFileFactory_Foo() { _name = "foo"; _exts = {"foo", "bar"}; }
 *     bool canDoOperation(const Context &ctx) const override { ... }
 *     Result<MyFile> createForOperation(const Context &ctx) const override { ... }
 * };
 * PROMEKI_REGISTER_MYFILE_FACTORY(MyFileFactory_Foo);
 * @endcode
 */
template<typename Product>
class FileFormatFactory {
        public:
                /**
                 * @brief Context passed to factory methods for lookup and creation.
                 *
                 * Carries all available information about the requested operation.
                 * Factories inspect whichever fields are relevant and ignore the rest.
                 */
                struct Context {
                        int         operation = 0;    ///< @brief Product-specific operation (e.g. Reader/Writer).
                        String      filename;         ///< @brief Full path, if available.
                        String      formatHint;       ///< @brief Extension hint (e.g. "wav"), no dot.
                        IODevice   *device = nullptr; ///< @brief IODevice to operate on, if available.
                };

                /**
                 * @brief Registers a factory instance in the global registry.
                 * @param factory Pointer to the factory to register. Ownership is taken.
                 * @return A non-zero value (used for static initialization).
                 */
                static int registerFactory(FileFormatFactory *factory) {
                        if(factory == nullptr) return -1;
                        auto &list = factoryList();
                        int ret = list.size();
                        list += factory;
                        promekiLog(Logger::LogLevel::Debug, "Registered FileFormatFactory %s",
                                factory->name().cstr());
                        return ret;
                }

                /**
                 * @brief Looks up a factory that can handle the given context.
                 *
                 * Iterates registered factories, calling canDoOperation() on each.
                 * Returns the first match or nullptr if none found.
                 * @param ctx The context describing the requested operation.
                 * @return A pointer to a matching factory, or nullptr.
                 */
                static const FileFormatFactory *lookup(const Context &ctx) {
                        auto &list = factoryList();
                        for(auto *f : list) {
                                if(f->canDoOperation(ctx)) return f;
                        }
                        return nullptr;
                }

                /** @brief Default constructor. */
                FileFormatFactory() = default;

                /** @brief Virtual destructor. */
                virtual ~FileFormatFactory() {}

                /**
                 * @brief Returns the human-readable name of this factory.
                 * @return The factory name as a String.
                 */
                String name() const { return _name; }

                /**
                 * @brief Returns the list of supported file extensions.
                 * @return A const reference to the extension list.
                 */
                const StringList &extensions() const { return _exts; }

                /**
                 * @brief Returns true if the filename has a supported extension.
                 * @param filename The path to check.
                 * @return true if the file extension matches one of this factory's extensions.
                 */
                bool isExtensionSupported(const String &filename) const {
                        size_t dot = filename.rfind('.');
                        if(dot == String::npos || dot + 1 >= filename.size()) return false;
                        String ext = filename.mid(dot + 1).toLower();
                        for(const auto &item : _exts) {
                                if(ext == item) return true;
                        }
                        return false;
                }

                /**
                 * @brief Returns true if the hint matches a supported extension.
                 * @param hint The format hint string (e.g. "wav"), no dot.
                 * @return true if the hint matches one of this factory's extensions.
                 */
                bool isHintSupported(const String &hint) const {
                        String lower = hint.toLower();
                        for(const auto &item : _exts) {
                                if(lower == item) return true;
                        }
                        return false;
                }

                /**
                 * @brief Returns true if this factory can handle the given context.
                 *
                 * Subclasses override this to inspect the context and decide
                 * whether they can perform the requested operation.
                 * @param ctx The context describing the requested operation.
                 * @return true if the operation is supported.
                 */
                virtual bool canDoOperation(const Context &ctx) const {
                        return false;
                }

                /**
                 * @brief Creates a Product configured for the given context.
                 *
                 * Subclasses override this to instantiate their backend.
                 * @param ctx The context describing the requested operation.
                 * @return A Result containing the product on success, or an error.
                 */
                virtual Result<Product> createForOperation(const Context &ctx) const {
                        return makeError<Product>(Error::NotImplemented);
                }

        protected:
                String     _name;  ///< @brief Human-readable factory name.
                StringList _exts;  ///< @brief List of supported file extensions (no dots).

        private:
                static List<FileFormatFactory *> &factoryList() {
                        static List<FileFormatFactory *> list;
                        return list;
                }
};

/**
 * @brief Registers a FileFormatFactory subclass at static initialization time.
 * @param product The product type (e.g. AudioFile).
 * @param name The factory subclass to instantiate and register.
 */
#define PROMEKI_REGISTER_FILE_FORMAT_FACTORY(product, name) \
        [[maybe_unused]] static int \
        PROMEKI_CONCAT(__promeki_fff_, PROMEKI_UNIQUE_ID) = \
        FileFormatFactory<product>::registerFactory(new name);

PROMEKI_NAMESPACE_END
