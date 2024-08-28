#[macro_export]
macro_rules! define_table {
    (
        $(#[$meta:meta])*
        $vis:vis struct $name:ident {
            key_type = $key_type:ty,
            value_type = $value_type:ty,
            $(custom_key = { $($custom_key:tt)* },)?
            $(custom_value = { $($custom_value:tt)* },)?
        }
        $(, SecondaryIndex = $primary_column:ty)?
        $(, InitialKeyProvider = |$pk:ident: $pk_type:ty| $initial_key_expr:expr)?
    ) => {
        // Repository definition
        $(#[$meta])*
        $vis struct $name {
            pub store: Arc<OceanStore>,
            col: LedgerColumn<$name>,
        }

        impl ColumnName for $name {
            const NAME: &'static str = stringify!($name);
        }

        impl Column for $name {
            type Index = $key_type;

            $(
                $($custom_key)*
            )?
        }

        impl TypedColumn for $name {
            type Type = $value_type;

            $(
                $($custom_value)*
            )?
        }


        impl $name {
            pub fn new(store: Arc<OceanStore>) -> Self {
                Self {
                    col: store.column(),
                    store,
                }
            }

            $(
                #[allow(unused)]
                pub fn get_highest(&self) -> Result<Option<<$primary_column as TypedColumn>::Type>> {
                    match self.col.iter(None, SortOrder::Descending.into())?.next() {
                        None => Ok(None),
                        Some(Ok((_, id))) => {
                            let col = self.store.column::<$primary_column>();
                            Ok(col.get(&id)?)
                        }
                        Some(Err(e)) => Err(e.into()),
                    }
                }
            )?
        }

        impl RepositoryOps<$key_type, $value_type> for $name {
            type ListItem = std::result::Result<($key_type, $value_type), ain_db::DBError>;

            fn get(&self, id: &$key_type) -> Result<Option<$value_type>> {
                Ok(self.col.get(id)?)
            }

            fn put(&self, id: &$key_type, item: &$value_type) -> Result<()> {
                Ok(self.col.put(id, item)?)
            }

            fn delete(&self, id: &$key_type) -> Result<()> {
                Ok(self.col.delete(id)?)
            }

            fn list<'a>(&'a self, from: Option<$key_type>, dir: $crate::storage::SortOrder) -> Result<Box<dyn Iterator<Item = Self::ListItem> + 'a>> {
                let it = self.col.iter(from, dir.into())?;
                Ok(Box::new(it))
            }
        }

        $(
            impl SecondaryIndex<$key_type, $value_type> for $name {
                type Value = <$primary_column as TypedColumn>::Type;

                fn retrieve_primary_value(&self, el: Self::ListItem) -> Result<Self::Value> {
                    let (_, id) = el?;
                    let col = self.store.column::<$primary_column>();
                    let value = col.get(&id)?.ok_or(Error::SecondaryIndex)?;
                    Ok(value)
                }
            }
        )?

        $(
            impl InitialKeyProvider<$key_type, $value_type> for $name {
                type PartialKey = $pk_type;

                fn initial_key($pk: Self::PartialKey) -> $key_type {
                    $initial_key_expr
                }
            }
        )?
    };
}
